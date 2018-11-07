#ifndef PROGRESSBAR_HPP
#define PROGRESSBAR_HPP

class ProgressBar {
    using Progress = unsigned int;
public:
    /// usage:
    /// ProgressBar() << currentProgress;
    ProgressBar(Progress progress);
    ~ProgressBar();

    /// @brief display current progress to terminal
    void display() const;


    /// @brief if progressbar not displayed print progressbar
private:
    Progress ticks = 0;
    const Progress totalTicks = 100;
    const Progress barWidth = 50;
    const char completeSymbol = ' ';
    const char incompleteSymbol = ' ';
};

#endif // PROGRESSBAR_HPP
