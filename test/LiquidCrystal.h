#pragma once
#include "Arduino.h"

class LiquidCrystal {
public:
  LiquidCrystal(int, int, int, int, int, int) {}
  void begin(int, int) {}
  void clear() {
    g_lcdRow[0].clear();
    g_lcdRow[1].clear();
    g_lcdCursorRow = 0;
    g_lcdCursorCol = 0;
  }
  void setCursor(int col, int row) { g_lcdCursorCol = col; g_lcdCursorRow = row; }
  void print(const char* s) { emit(std::string(s)); }
  void print(int v) { emit(std::to_string(v)); }
  void print(unsigned long v) { emit(std::to_string(v)); }
private:
  void emit(const std::string& s) {
    std::string& row = g_lcdRow[g_lcdCursorRow];
    while ((int)row.size() < g_lcdCursorCol) row += ' ';
    row.replace(g_lcdCursorCol, s.size(), s);
    g_lcdCursorCol += (int)s.size();
  }
};
