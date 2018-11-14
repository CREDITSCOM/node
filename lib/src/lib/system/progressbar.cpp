#include "lib/system/progressbar.hpp"

#include <rang.hpp>

ProgressBar::ProgressBar(
    char completeSymbol, char incompleteSymbol, unsigned totalProgressLimit, unsigned barWidthInSymbols)
: completeSymbol(completeSymbol)
, incompleteSymbol(incompleteSymbol)
, totalTicks(totalProgressLimit)
, barWidth(barWidthInSymbols)
{
    rang::setControlMode(rang::control::Force);
    rang::setWinTermMode(rang::winTerm::Ansi);
}

std::string ProgressBar::string(ProgressBar::Progress ticks) {
    float progress = float(ticks) / totalTicks;
    auto pos = unsigned(barWidth * progress);

    std::string completed(pos, completeSymbol);
    std::string incompleted(barWidth - pos, incompleteSymbol);

    std::stringstream result;
    result << rang::bg::blue << completed;
    result << rang::bg::gray << incompleted;
    result << rang::bg::reset << rang::fg::reset << rang::style::reset;
    result << " " << (progress * 100.0f) << "%";

    result.flush();
    result << std::endl;
    return result.str();
}

ProgressBar::~ProgressBar() {
    rang::setControlMode(rang::control::Auto);
    rang::setWinTermMode(rang::winTerm::Auto);
}
