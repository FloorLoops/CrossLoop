/* Header/API compile gate for the parser/style dependency layer only. */
#include <stdbool.h>
#include <stddef.h>

#include <libwapcaplet/libwapcaplet.h>
#include <parserutils/input/inputstream.h>
#include <hubbub/parser.h>
#include <dom/dom.h>
#include <libcss/stylesheet.h>
#include <nsutils/base64.h>

size_t netsurf_native_api_probe(void)
{
    return sizeof(lwc_string *) + sizeof(parserutils_inputstream *) +
           sizeof(hubbub_parser *) + sizeof(dom_document *) +
           sizeof(css_stylesheet_params) + sizeof(nsuerror);
}
