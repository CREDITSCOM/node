#ifndef PROGRESSBAR_HPP
#define PROGRESSBAR_HPP

#include <sstream>

class ProgressBar {
    using Progress = unsigned int;

public:
    ProgressBar(const ProgressBar&) = default;
    ProgressBar(ProgressBar&&) = default;
    ProgressBar& operator=(const ProgressBar&) = default;
    ProgressBar& operator=(ProgressBar&&) = default;

    /// @brief ProgressBar
    /// @param completeSymbol symbol that mark completed progress
    /// @param incompleteSymbol symbol that mark incompleted bar ticks
    /// @param totalProgressLimit in other words total progress percentage of completion
    /// @param barWidthInSymbols displayed real width of progress bar in symbols
    explicit ProgressBar(char completeSymbol = '#', char incompleteSymbol = '_', unsigned totalProgressLimit = 100, unsigned barWidthInSymbols = 50);
    ~ProgressBar();

    /// @brief string
    /// @param ticks current completed progress in ticks
    /// @return symbolic (std::string) progress representation
    std::string string(Progress ticks);

    char completeSymbol;
    char incompleteSymbol;
    Progress totalTicks;
    Progress barWidth;
};

#endif  // PROGRESSBAR_HPP
