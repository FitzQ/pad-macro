
#pragma once
#include <tesla.hpp>
#include <switch.h>
#include <cstring>
#include <cstdio>


struct ButtonGlyph {
    u64 button;
    const char* glyph;
};
constexpr array<ButtonGlyph, 18> ButtonGlyphArr = {{
    { HidNpadButton_A, "\uE0A0" },
    { HidNpadButton_B, "\uE0A1" },
    { HidNpadButton_X, "\uE0A2" },
    { HidNpadButton_Y, "\uE0A3" },
    { HidNpadButton_StickL, "\uE0C4" },
    { HidNpadButton_StickR, "\uE0C5" },
    { HidNpadButton_L, "\uE0A4" },
    { HidNpadButton_R, "\uE0A5" },
    { HidNpadButton_ZL, "\uE0A6" },
    { HidNpadButton_ZR, "\uE0A7" },
    { HidNpadButton_Plus, "\uE0B5" },
    { HidNpadButton_Minus, "\uE0B6" },
    { HidNpadButton_Left, "\uE0B1" },
    { HidNpadButton_Up, "\uE0AF" },
    { HidNpadButton_Right, "\uE0B2" },
    { HidNpadButton_Down, "\uE0B0" }
}};

constexpr array<ButtonGlyph, 18> PseudoButtonGlyphArr = {{
    { HidNpadButton_StickLLeft, "\uE091" },
    { HidNpadButton_StickLUp, "\uE092" },
    { HidNpadButton_StickLRight, "\uE090" },
    { HidNpadButton_StickLDown, "\uE093" },
    
    // { HidNpadButton_StickRLeft, "\uE091" },
    // { HidNpadButton_StickRUp, "\uE092" },
    // { HidNpadButton_StickRRight, "\uE090" },
    // { HidNpadButton_StickRDown, "\uE093" }
    // libnx type mismatch
    { HidNpadButton_LeftSL, "\uE091" },
    { HidNpadButton_LeftSR, "\uE092" },
    { HidNpadButton_RightSL, "\uE090" },
    { HidNpadButton_RightSR, "\uE093" }
}};

string maskToGlyph(const u64 mask) {
    string glyphCombo;
    for (const ButtonGlyph &buttonGlyph : ButtonGlyphArr) {
        if (mask & buttonGlyph.button) {
            // if (!glyphCombo.empty())
            //     glyphCombo += "\u002B"; // \u002B
            glyphCombo += buttonGlyph.glyph;
        }
    }
    return glyphCombo;
}

void splitMask(const u64 mask, u64* left, u64* right) {
    *left = (mask & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_StickR
        | HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_Plus
        | HidNpadButton_StickLLeft | HidNpadButton_StickLUp | HidNpadButton_StickLRight | HidNpadButton_StickLDown));
    *right = (mask & (HidNpadButton_Left | HidNpadButton_Up | HidNpadButton_Right | HidNpadButton_Down | HidNpadButton_StickL
        | HidNpadButton_L | HidNpadButton_ZL | HidNpadButton_Minus
        | HidNpadButton_LeftSL | HidNpadButton_LeftSR | HidNpadButton_RightSL | HidNpadButton_RightSR));
}

string pseudoMaskToGlyph(const u64 mask) {
    string glyphCombo;
    for (const ButtonGlyph &buttonGlyph : PseudoButtonGlyphArr) {
        if (mask & buttonGlyph.button) {
            // if (!glyphCombo.empty())
            //     glyphCombo += "\u002B"; // \u002B
            glyphCombo += buttonGlyph.glyph;
        }
    }
    glyphCombo = glyphCombo == "\uE091\uE092" ? "\uE097" : glyphCombo;
    glyphCombo = glyphCombo == "\uE092\uE090" ? "\uE094" : glyphCombo;
    glyphCombo = glyphCombo == "\uE090\uE093" ? "\uE095" : glyphCombo;
    glyphCombo = glyphCombo == "\uE091\uE093" ? "\uE096" : glyphCombo;
    return glyphCombo;
}
