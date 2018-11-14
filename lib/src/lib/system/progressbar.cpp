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
}

ProgressBar::~ProgressBar() {
    rang::setControlMode(rang::control::Auto);
}

std::string ProgressBar::string(ProgressBar::Progress ticks) {
    float progress = float(ticks) / totalTicks;
    auto pos = unsigned(barWidth * progress);

    std::string completed(pos, completeSymbol);
    std::string incompleted(barWidth - pos, incompleteSymbol);

    std::stringstream result;
    result << rang::bg::blue << completed;
    result << rang::bg::black << incompleted;
    result << rang::bg::reset << rang::fg::reset << rang::style::reset;
    result << " " << (progress * 100.0f) << "%";

    result.flush();
    result << std::endl;
    return result.str();
}
