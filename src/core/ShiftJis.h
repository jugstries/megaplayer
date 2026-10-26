#pragma once

#include <string>
#include <string_view>

namespace mp {

// Il testo Shift-JIS dei computer giapponesi (PC-98, X68000, FM Towns), nella
// variante cp932 con i caratteri speciali NEC, in UTF-8. Si ferma al primo
// zero; i byte che non formano un carattere diventano "?".
std::string shiftJisToUtf8(std::string_view text);

} // namespace mp
