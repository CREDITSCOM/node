#include "lib/system/progressbar.hpp"

#include <rang.hpp>
#include <iostream>

ProgressBar::ProgressBar(
    char completeSymbol, char incompleteSymbol, unsigned totalProgressLimit, unsigned barWidthInSymbols)
: completeSymbol(completeSymbol)
, incompleteSymbol(incompleteSymbol)
, totalTicks(totalProgressLimit)
, barWidth(barWidthInSymbols)
{
    rang::setControlMode(rang::control::Force);
    rang::setWinTermMode(rang::winTerm::Ansi);
#ifdef _WINDOWS
    //FIXME: it's a temporary hack to turn on ANSI sequences on windows terminal
    //because bar outputs progress to _any_ stream, so we need to force ansi 
    rang::rang_implementation::setWinTermAnsiColors(std::cout.rdbuf());
#endif
}

ProgressBar::~ProgressBar() {
    rang::setControlMode(rang::control::Auto);
    rang::setWinTermMode(rang::winTerm::Auto);
}

std::string ProgressBar::string(ProgressBar::Progress ticks) {
    float progress = float(ticks) / totalTicks;
    auto pos = unsigned(barWidth * progress);

    std::string completed(pos, completeSymbol);
    std::string incompleted(barWidth - pos, incompleteSymbol);

    std::stringstream result;
    result << rang::bg::blue  << rang::fg::blue << completed;
    result << rang::bg::gray  << rang::fg::gray << incompleted;
    result << rang::bg::reset << rang::fg::reset << rang::style::reset;
    result << " " << (progress * 100.0f) << "%";

    result.flush();
    result << std::endl;

    return result.str();
}
