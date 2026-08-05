#include "instance/game/Board.h"

namespace nhn::instance {

void Board::Reset(uint8 width, uint8 height, uint8 winLength) {
    _width = width;
    _height = height;
    _winLength = winLength;
    _claimed = 0;
    _cells.assign(static_cast<size_t>(width) * height, kNoOwner);
}

uint8 Board::OwnerAt(uint16 cellIndex) const {
    if (cellIndex >= _cells.size()) {
        return kNoOwner;
    }
    return _cells[cellIndex];
}

bool Board::Claim(uint16 cellIndex, uint8 slot) {
    if (cellIndex >= _cells.size() || _cells[cellIndex] != kNoOwner) {
        return false;
    }
    _cells[cellIndex] = slot;
    ++_claimed;
    return true;
}

uint8 Board::CountRun(int32 x, int32 y, int32 dx, int32 dy, uint8 slot) const {
    uint8 count = 0;
    for (;;) {
        x += dx;
        y += dy;
        if (x < 0 || y < 0 || x >= _width || y >= _height) {
            break;
        }
        if (_cells[static_cast<size_t>(y) * _width + x] != slot) {
            break;
        }
        ++count;
        // Cannot exceed the board, but stop early once a win is certain.
        if (count >= _winLength) {
            break;
        }
    }
    return count;
}

bool Board::CompletesLine(uint16 cellIndex, uint8 slot) const {
    if (cellIndex >= _cells.size() || _cells[cellIndex] != slot) {
        return false;
    }

    const int32 x = cellIndex % _width;
    const int32 y = cellIndex / _width;

    // Horizontal, vertical, and both diagonals. Each is counted in both
    // directions and includes the cell itself.
    static constexpr int32 kAxes[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    for (const auto& axis : kAxes) {
        const uint8 run = 1 + CountRun(x, y, axis[0], axis[1], slot) +
                          CountRun(x, y, -axis[0], -axis[1], slot);
        if (run >= _winLength) {
            return true;
        }
    }
    return false;
}

}  // namespace nhn::instance
