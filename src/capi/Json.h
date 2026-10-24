#pragma once

#include <optional>
#include <string>

namespace mp::json {

// Una stringa JSON tra virgolette. I nomi dei moduli Amiga sono spesso in
// Latin-1: le sequenze UTF-8 non valide vengono lette come Latin-1.
std::string string(const std::string& text);

// Un numero con tre decimali, o null.
std::string number(std::optional<double> value);

} // namespace mp::json
