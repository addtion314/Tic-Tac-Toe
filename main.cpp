#pragma comment(lib, "msimg32.lib") 

#include <graphics.h>
#include <math.h>
#include <tchar.h>
#include <stdlib.h>

const int BOARD_SIZE = 600;
const int CELL = BOARD_SIZE / 3;

// Game State Machine
enum GameState {
    PLAYING,
    END_DELAY,
    BANNER_ENTER,
    RESETTING
};

GameState state = PLAYING;
int delayFrames = 0;

int board[3][3] = { 0 };
float animScale[3][3] = { 0 };
int turn = 1;
int winner = 0;

// Win Highlight State
bool winCells[3][3] = { false };
float winAlpha = 0.0f; // Controls the fade-in of the golden background

// Banner State
float animBannerY = -150.0f;
float animBannerVy = 0.0f;

// Blur State
IMAGE blurredBoard;
float blurAlpha = 0.0f;

// Physics State for pieces falling
float physX[3][3] = { 0 };
float physY[3][3] = { 0 };
float physVX[3][3] = { 0 };
float physVY[3][3] = { 0 };
float physAngle[3][3] = { 0 };
float physVA[3][3] = { 0 };

// --------------------------------------------------------
// Ultra-fast O(N) BoxBlur approximating Gaussian Blur
// --------------------------------------------------------
void BoxBlurH(DWORD* src, DWORD* dst, int w, int h, int radius) {
    int windowSize = 2 * radius + 1;
    for (int y = 0; y < h; y++) {
        int sum0 = 0, sum1 = 0, sum2 = 0;
        int offset = y * w;
        for (int i = -radius; i <= radius; i++) {
            int px = max(0, min(w - 1, i));
            DWORD c = src[offset + px];
            sum0 += c & 0xFF; sum1 += (c >> 8) & 0xFF; sum2 += (c >> 16) & 0xFF;
        }
        for (int x = 0; x < w; x++) {
            dst[offset + x] = (sum0 / windowSize) | ((sum1 / windowSize) << 8) | ((sum2 / windowSize) << 16);
            int next = max(0, min(w - 1, x + radius + 1)), prev = max(0, min(w - 1, x - radius));
            DWORD cNext = src[offset + next], cPrev = src[offset + prev];
            sum0 += (cNext & 0xFF) - (cPrev & 0xFF);
            sum1 += ((cNext >> 8) & 0xFF) - ((cPrev >> 8) & 0xFF);
            sum2 += ((cNext >> 16) & 0xFF) - ((cPrev >> 16) & 0xFF);
        }
    }
}
void BoxBlurV(DWORD* src, DWORD* dst, int w, int h, int radius) {
    int windowSize = 2 * radius + 1;
    for (int x = 0; x < w; x++) {
        int sum0 = 0, sum1 = 0, sum2 = 0;
        for (int i = -radius; i <= radius; i++) {
            int py = max(0, min(h - 1, i));
            DWORD c = src[py * w + x];
            sum0 += c & 0xFF; sum1 += (c >> 8) & 0xFF; sum2 += (c >> 16) & 0xFF;
        }
        for (int y = 0; y < h; y++) {
            dst[y * w + x] = (sum0 / windowSize) | ((sum1 / windowSize) << 8) | ((sum2 / windowSize) << 16);
            int next = max(0, min(h - 1, y + radius + 1)), prev = max(0, min(h - 1, y - radius));
            DWORD cNext = src[next * w + x], cPrev = src[prev * w + x];
            sum0 += (cNext & 0xFF) - (cPrev & 0xFF);
            sum1 += ((cNext >> 8) & 0xFF) - ((cPrev >> 8) & 0xFF);
            sum2 += ((cNext >> 16) & 0xFF) - ((cPrev >> 16) & 0xFF);
        }
    }
}
void ApplyFastBlur(IMAGE* img) {
    int w = img->getwidth(), h = img->getheight();
    DWORD* buf = GetImageBuffer(img);
    DWORD* temp = new DWORD[w * h];
    int r = 10;
    BoxBlurH(buf, temp, w, h, r); BoxBlurV(temp, buf, w, h, r);
    BoxBlurH(buf, temp, w, h, r); BoxBlurV(temp, buf, w, h, r);
    delete[] temp;
}
// --------------------------------------------------------

bool CheckWin(int player) {
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) winCells[r][c] = false;
    for (int i = 0; i < 3; i++) {
        if (board[i][0] == player && board[i][1] == player && board[i][2] == player) { winCells[i][0] = winCells[i][1] = winCells[i][2] = true; return true; }
        if (board[0][i] == player && board[1][i] == player && board[2][i] == player) { winCells[0][i] = winCells[1][i] = winCells[2][i] = true; return true; }
    }
    if (board[0][0] == player && board[1][1] == player && board[2][2] == player) { winCells[0][0] = winCells[1][1] = winCells[2][2] = true; return true; }
    if (board[0][2] == player && board[1][1] == player && board[2][0] == player) { winCells[0][2] = winCells[1][1] = winCells[2][0] = true; return true; }
    return false;
}

bool CheckTie() {
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            if (board[r][c] == 0) return false;
        }
    }
    return true;
}

// Triggered when user clicks to restart
void InitReset() {
    state = RESETTING;
    animBannerVy = -8.0f; // Give banner a slight popup bounce before falling

    // Init physics for all pieces
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            if (board[r][c] != 0) {
                physX[r][c] = c * CELL + CELL / 2.0f;
                physY[r][c] = r * CELL + CELL / 2.0f;
                physVX[r][c] = (rand() % 100 - 50) * 0.1f;         // Random X spread
                physVY[r][c] = -12.0f - (rand() % 100) * 0.05f;    // Pop upwards
                physAngle[r][c] = 0.0f;
                physVA[r][c] = (rand() % 100 - 50) * 0.003f;       // Random rotation speed
            }
        }
    }
}

// Fully clean up and return to playing
void FullyClearGame() {
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) { board[r][c] = 0; animScale[r][c] = 0.0f; winCells[r][c] = false; }
    }
    turn = 1;
    state = PLAYING;
    winner = 0;
    delayFrames = 0;
    winAlpha = 0.0f;
    animBannerY = -150.0f;
    blurAlpha = 0.0f;
}

void DrawX(float x, float y, float scale, float angle) {
    setlinecolor(RGB(255, 71, 87));
    setlinestyle(PS_SOLID, 10);
    float len = 45.0f * scale;
    float c1 = cos(angle - 0.785398f), s1 = sin(angle - 0.785398f);
    float c2 = cos(angle + 0.785398f), s2 = sin(angle + 0.785398f);
    line((int)(x - len * c1), (int)(y - len * s1), (int)(x + len * c1), (int)(y + len * s1));
    line((int)(x - len * c2), (int)(y - len * s2), (int)(x + len * c2), (int)(y + len * s2));
}

void DrawO(float x, float y, float scale, float angle) {
    setlinecolor(RGB(30, 144, 255));
    setlinestyle(PS_SOLID, 10);
    int radius = (int)(55 * scale);
    if (radius > 0) circle((int)x, (int)y, radius);
}

int main() {
    initgraph(BOARD_SIZE, BOARD_SIZE);
    BeginBatchDraw();

    while (true) {
        ExMessage msg;
        while (peekmessage(&msg, EM_MOUSE | EM_KEY)) {
            if (state == BANNER_ENTER) {
                if (msg.message == WM_LBUTTONDOWN || msg.message == WM_KEYDOWN) InitReset();
            }
            else if (state == PLAYING) {
                if (msg.message == WM_LBUTTONDOWN) {
                    int c = msg.x / CELL, r = msg.y / CELL;
                    if (board[r][c] == 0) {
                        board[r][c] = turn;
                        if (CheckWin(turn)) { state = END_DELAY; winner = turn; }
                        else if (CheckTie()) { state = END_DELAY; winner = 0; }
                        else { turn = (turn == 1) ? 2 : 1; }
                    }
                }
            }
        }

        // --- Logic and Animation Updates ---

        if (state == PLAYING || state == END_DELAY || state == BANNER_ENTER) {
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    if (board[r][c] != 0) animScale[r][c] += (1.0f - animScale[r][c]) * 0.15f;
                }
            }
        }

        if (state == END_DELAY) {
            winAlpha += (1.0f - winAlpha) * 0.08f; // Fade in golden background
            delayFrames++;
            if (delayFrames > 75) {
                // Take screenshot before banner drops, prepare blur
                getimage(&blurredBoard, 0, 0, BOARD_SIZE, BOARD_SIZE);
                ApplyFastBlur(&blurredBoard);
                state = BANNER_ENTER;
            }
        }
        else if (state == BANNER_ENTER) {
            float targetBannerY = BOARD_SIZE / 2;
            animBannerY += (targetBannerY - animBannerY) * 0.1f;
            blurAlpha += (1.0f - blurAlpha) * 0.08f;
        }
        else if (state == RESETTING) {
            winAlpha += (0.0f - winAlpha) * 0.1f; // Fade out background
            blurAlpha += (0.0f - blurAlpha) * 0.12f; // Fade out blur revealing physics

            animBannerVy += 1.2f; // Banner Gravity
            animBannerY += animBannerVy;

            bool allClear = true;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    if (board[r][c] != 0) {
                        physVY[r][c] += 0.8f; // Pieces Gravity
                        physX[r][c] += physVX[r][c];
                        physY[r][c] += physVY[r][c];
                        physAngle[r][c] += physVA[r][c];
                        if (physY[r][c] < BOARD_SIZE + 100) allClear = false;
                    }
                }
            }
            if (animBannerY < BOARD_SIZE + 200 || blurAlpha > 0.01f) allClear = false;

            if (allClear) FullyClearGame();
        }

        // --- Render Base Layer ---
        setbkcolor(RGB(30, 30, 30));
        cleardevice();

        // Render Golden Winning Highlight
        if ((state >= END_DELAY || state == RESETTING) && winner != 0) {
            float mix = winAlpha * 0.6f; // Max opacity is 60%
            int rBg = (int)(30 + (255 - 30) * mix);
            int gBg = (int)(30 + (215 - 30) * mix);
            int bBg = (int)(30 + (0 - 30) * mix);
            setfillcolor(RGB(rBg, gBg, bBg));
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    if (winCells[r][c]) {
                        solidrectangle(c * CELL + 2, r * CELL + 2, (c + 1) * CELL - 2, (r + 1) * CELL - 2);
                    }
                }
            }
        }

        // Render Grid
        setlinecolor(RGB(70, 70, 70));
        setlinestyle(PS_SOLID, 4);
        line(CELL, 0, CELL, BOARD_SIZE); line(CELL * 2, 0, CELL * 2, BOARD_SIZE);
        line(0, CELL, BOARD_SIZE, CELL); line(0, CELL * 2, BOARD_SIZE, CELL * 2);

        // Render Pieces
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                if (board[r][c] == 0) continue;

                float px, py, scale, angle;
                if (state == RESETTING) {
                    px = physX[r][c]; py = physY[r][c];
                    scale = animScale[r][c]; angle = physAngle[r][c];
                }
                else {
                    px = c * CELL + CELL / 2.0f; py = r * CELL + CELL / 2.0f;
                    scale = animScale[r][c]; angle = 0.0f;
                }

                if (board[r][c] == 1) DrawX(px, py, scale, angle);
                if (board[r][c] == 2) DrawO(px, py, scale, angle);
            }
        }

        // --- Render Effects Layer (Blur and Banner) ---
        if (state == BANNER_ENTER || state == RESETTING) {
            if (blurAlpha > 0.01f) {
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(blurAlpha * 255), 0 };
                AlphaBlend(GetImageHDC(NULL), 0, 0, BOARD_SIZE, BOARD_SIZE,
                    GetImageHDC(&blurredBoard), 0, 0, BOARD_SIZE, BOARD_SIZE, bf);
            }

            if (animBannerY > -150 && animBannerY < BOARD_SIZE + 200) {
                int topY = (int)animBannerY - 60;
                int bottomY = (int)animBannerY + 60;

                setfillcolor(RGB(45, 45, 45));
                solidrectangle(0, topY, BOARD_SIZE, bottomY);

                COLORREF borderColor = RGB(150, 150, 150);
                if (winner == 1) borderColor = RGB(255, 71, 87);
                else if (winner == 2) borderColor = RGB(30, 144, 255);

                setlinecolor(borderColor);
                setlinestyle(PS_SOLID, 4);
                line(0, topY, BOARD_SIZE, topY);
                line(0, bottomY, BOARD_SIZE, bottomY);

                setbkmode(TRANSPARENT);
                settextcolor(RGB(255, 255, 255));
                settextstyle(45, 0, _T("Consolas"), 0, 0, FW_BOLD, false, false, false);

                LPCTSTR titleText = _T("DRAW!");
                if (winner == 1) titleText = _T("RED WINS!");
                else if (winner == 2) titleText = _T("BLUE WINS!");

                int titleW = textwidth(titleText);
                int titleH = textheight(titleText);
                outtextxy(BOARD_SIZE / 2 - titleW / 2, (int)animBannerY - titleH / 2 - 10, titleText);

                settextstyle(20, 0, _T("Consolas"));
                settextcolor(RGB(180, 180, 180));
                LPCTSTR hintText = _T("- Press Any Key to Restart -");
                int hintW = textwidth(hintText);
                outtextxy(BOARD_SIZE / 2 - hintW / 2, (int)animBannerY + 25, hintText);
            }
        }

        FlushBatchDraw();
        Sleep(16);
    }

    return 0;
}
