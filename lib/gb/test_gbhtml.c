/* Host tests for the bounded streaming HTML subset parser. */
#include <stdio.h>
#include <string.h>

static char visible[1024];
static char title[128];
static char link_url[128];
static char image_src[128];
static char form_attrs[128], first_input_attrs[128], second_input_attrs[128];
static unsigned int visible_len, title_len;
static unsigned int link_begin_count, link_end_count;
static unsigned int image_count;
static unsigned int form_count, input_count, form_close_count;
static unsigned char table_events[32];
static unsigned char table_event_count;

static void emit_text(unsigned char c);
static void emit_title(unsigned char c);
static void emit_break(unsigned char kind);
static void link_begin(const char *url);
static void link_end(void);
static void image_alt(const char *alt);
static void image(const char *src, const char *alt);
static void form_tag(unsigned char kind, unsigned char attr_start);
static void table_tag(unsigned char kind, unsigned char attr_start);

#define GB_HTML_EMIT_TEXT(c) emit_text(c)
#define GB_HTML_EMIT_TITLE(c) emit_title(c)
#define GB_HTML_EMIT_BREAK(kind) emit_break(kind)
#define GB_HTML_LINK_BEGIN(url) link_begin(url)
#define GB_HTML_LINK_END() link_end()
#define GB_HTML_IMAGE_ALT(alt) image_alt(alt)
#define GB_HTML_IMAGE(src, alt) image(src, alt)
#define GB_HTML_FORM_TAG(kind, attrs) form_tag(kind, attrs)
#define GB_HTML_TABLE_TAG(kind, attrs) table_tag(kind, attrs)
#include "gbhtml.h"

static int failures;

static void check(int ok, const char *name)
{
    if (ok) printf("ok   %s\n", name);
    else { printf("FAIL %s\n", name); failures++; }
}

static void emit_text(unsigned char c)
{
    if (visible_len + 1 < sizeof(visible)) visible[visible_len++] = (char)c;
}

static void emit_title(unsigned char c)
{
    if (title_len + 1 < sizeof(title)) title[title_len++] = (char)c;
}

static void emit_break(unsigned char kind)
{
    (void)kind;
    if (visible_len && visible[visible_len - 1] != '\n') emit_text('\n');
}

static void link_begin(const char *url)
{
    link_begin_count++;
    strncpy(link_url, url, sizeof(link_url) - 1);
    link_url[sizeof(link_url) - 1] = 0;
}

static void link_end(void)
{
    link_end_count++;
}

static void image_alt(const char *alt)
{
    while (*alt) emit_text((unsigned char)*alt++);
}

static void image(const char *src, const char *alt)
{
    image_count++;
    strncpy(image_src, src, sizeof(image_src) - 1);
    image_src[sizeof(image_src) - 1] = 0;
    image_alt(alt);
}

static void form_tag(unsigned char kind, unsigned char attr_start)
{
    const char *attrs = gb_html_tag_buffer + attr_start;
    if (kind == GB_HTML_FORM_OPEN) {
        form_count++;
        strcpy(form_attrs, attrs);
    } else if (kind == GB_HTML_FORM_INPUT) {
        if (!input_count) strcpy(first_input_attrs, attrs);
        else if (input_count == 1) strcpy(second_input_attrs, attrs);
        input_count++;
    } else if (kind == GB_HTML_FORM_CLOSE) form_close_count++;
}

static void table_tag(unsigned char kind, unsigned char attr_start)
{
    (void)attr_start;
    if (table_event_count < sizeof(table_events))
        table_events[table_event_count++] = kind;
}

static void capture_reset(void)
{
    visible_len = title_len = 0;
    link_begin_count = link_end_count = 0;
    image_count = 0;
    form_count = input_count = form_close_count = 0;
    table_event_count = 0;
    visible[0] = title[0] = link_url[0] = image_src[0] = 0;
    form_attrs[0] = first_input_attrs[0] = second_input_attrs[0] = 0;
    gb_html_reset();
}

static void capture_end(void)
{
    gb_html_end();
    visible[visible_len] = 0;
    title[title_len] = 0;
}

static void feed_chunks(const char *text, unsigned char chunk)
{
    unsigned int len = (unsigned int)strlen(text), take;
    while (len) {
        take = len < chunk ? len : chunk;
        gb_html_feed((const unsigned char *)text, take);
        text += take;
        len -= take;
    }
}

static void test_document(void)
{
    const char *page =
        "<!doctype html><html><head><title> Test &amp; Demo </title>"
        "<style>body > p { color: red }</style></head><body>\n"
        "<h1>Hello&nbsp;world</h1><p>Visit "
        "<a href=/next?q=1&amp;x=2>next page</a>.</p>"
        "<!-- ignored > text --><img src=x alt='A &lt;picture&gt;'> <br>Done"
        "<script>if (a < b) document.write('bad');</script></body></html>";

    capture_reset();
    feed_chunks(page, 3);
    capture_end();
    check(!strcmp(title, "Test & Demo"),
          "title text and entities stream across chunks");
    check(!strcmp(visible, "Hello world\nVisit next page.\nA <picture>\nDone"),
          "text, blocks, comments, scripts and image alt are reduced");
    check(link_begin_count == 1 && link_end_count == 1 &&
              !strcmp(link_url, "/next?q=1&x=2"),
          "bounded link callbacks receive decoded href");
    check(image_count == 1 && !strcmp(image_src, "x"),
          "bounded image callbacks receive src and alt");
}

static void test_pre_and_entities(void)
{
    const char *page =
        "<pre> a  b\n c</pre><ul><li>One</li>"
        "<li>Two &bogus;</li></ul>Tail &#65; &#x42; &unfinished";
    capture_reset();
    feed_chunks(page, 1);
    capture_end();
    check(!strcmp(visible,
                  " a  b\n c\n* One\n* Two &bogus;\nTail A B &unfinished"),
          "preformatted text, lists and literal bad entities are stable");
}

static void test_attribute_boundaries(void)
{
    capture_reset();
    feed_chunks("<a title='ignore href=bad' href='/good'>link</a>", 2);
    capture_end();
    check(link_begin_count == 1 && link_end_count == 1 &&
              !strcmp(link_url, "/good") && !strcmp(visible, "link"),
          "attribute names inside quoted values are ignored");
}

static void test_bounds_and_recovery(void)
{
    char page[400];
    unsigned int i, n = 0;
    const char *prefix = "<a href='";
    const char *middle = "'>linked</a><div data='";
    const char *suffix = "'>after</div><!---->done";

    while (*prefix) page[n++] = *prefix++;
    for (i = 0; i < GB_HTML_URL_MAX + 2; i++) page[n++] = 'x';
    while (*middle) page[n++] = *middle++;
    for (i = 0; i < GB_HTML_TAG_MAX + 8; i++) page[n++] = 'y';
    while (*suffix) page[n++] = *suffix++;
    page[n] = 0;

    capture_reset();
    feed_chunks(page, 7);
    capture_end();
    check(link_begin_count == 0 && link_end_count == 0,
          "oversized href is ignored without opening a link");
    check(!strcmp(visible, "linkedafter\ndone"),
          "oversized tags and empty comments recover at the next delimiter");
}

static void test_form_tags(void)
{
    capture_reset();
    feed_chunks("<form action='/' method='GET'><input type=text name=q value='retro'>"
                "<input type=submit value='Find'></form>", 2);
    capture_end();
    check(form_count == 1 && input_count == 2 && form_close_count == 1 &&
              strstr(form_attrs, "action='/'") &&
              strstr(first_input_attrs, "name=q") &&
              strstr(second_input_attrs, "type=submit"),
          "form and input tags stream through bounded raw callbacks");

    capture_reset();
    feed_chunks("<form action='/post' method=post><input name=q><input type=submit></form>", 3);
    capture_end();
    check(form_count == 1 && input_count == 2 && form_close_count == 1,
          "raw form callbacks remain stable across chunk boundaries");
}

static void test_table_tags(void)
{
    static const unsigned char expected[] = {
        GB_HTML_TABLE_OPEN, GB_HTML_ROW_OPEN,
        GB_HTML_HEADER_OPEN, GB_HTML_HEADER_CLOSE,
        GB_HTML_CELL_OPEN, GB_HTML_CELL_CLOSE,
        GB_HTML_ROW_CLOSE, GB_HTML_TABLE_CLOSE
    };
    capture_reset();
    feed_chunks("<table><tbody><tr><th>A</th><td>B</td></tr></tbody></table>", 1);
    capture_end();
    check(table_event_count == sizeof(expected) &&
              !memcmp(table_events, expected, sizeof(expected)) &&
              !strcmp(visible, "AB"),
          "table, row and cell callbacks stream across chunk boundaries");
}

static void test_arbitrary_bytes(void)
{
    unsigned long seed = 0x1984UL;
    unsigned int i;
    capture_reset();
    for (i = 0; i < 20000; i++) {
        seed = seed * 1103515245UL + 12345UL;
        gb_html_feed_byte((unsigned char)(seed >> 16));
    }
    capture_end();
    check(gb_html_tag_len <= GB_HTML_TAG_MAX &&
              gb_html_entity_len <= GB_HTML_ENTITY_MAX &&
              gb_html_state <= GB_HTML_ST_COMMENT,
          "arbitrary byte streams stay within parser bounds");
}

int main(void)
{
    test_document();
    test_pre_and_entities();
    test_attribute_boundaries();
    test_bounds_and_recovery();
    test_form_tags();
    test_table_tags();
    test_arbitrary_bytes();
    if (failures) {
        printf("\n%d HTML test(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall HTML tests passed\n");
    return 0;
}
