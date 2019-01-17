#pragma once
#include <ostream>


class OutputPrinter {
public:
    OutputPrinter(std::ostream& outputStream) : stream(outputStream) {}

    void DisablePrinting() {
        active = false;
    }
    void EnablePrinting() {
        active = true;
    }

    template<typename T>
    const OutputPrinter& operator<<(const T& v) const { if (active) stream << v; return *this; }

private:
    bool active = true;

    // Underlying output stream.
    std::ostream& stream;
};