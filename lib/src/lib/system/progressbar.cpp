#include "lib/system/progressbar.hpp"
#include <iostream>
#include <rang.hpp>

ProgressBar::ProgressBar(ProgressBar::Progress progress) :ticks(progress) {}

ProgressBar::~ProgressBar() {
    display();
}

void ProgressBar::display() const {
    float progress = float(ticks) / totalTicks;
    unsigned pos = unsigned(barWidth * progress);

    std::cout << std::endl;
    for (auto i = 0u; i < barWidth; ++i) {
        if (i < pos) {
            std::cout << rang::bg::blue << completeSymbol;
        }
        else {
            std::cout << rang::bg::black << incompleteSymbol;
        }
    }

    std::cout << rang::bg::reset << " ";
    std::cout << (progress * 100.0f) << "% ";
    std::cout.flush();
    std::cout << std::endl;
}
