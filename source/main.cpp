#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <map>
#include <random>
#include <string>
#include <vector>

struct Tile {
    int suit;       // 0:万, 1:筒, 2:条, 3:字
    int val;        // 1..9 或 1..7(东南西北中发白)
    std::string label;
    bool operator<(const Tile& o) const {
        if (suit != o.suit) return suit < o.suit;
        return val < o.val;
    }
};

struct AIPlayer {
    std::string name;
    std::string bubble;
    std::vector<Tile> hand;
    std::vector<std::string> lines;
};

Mix_Music* gBgm = nullptr;
Mix_Chunk* gSfxPlay = nullptr;
Mix_Chunk* gSfxHu = nullptr;
Mix_Chunk* gSfxTaunt = nullptr;

void playSfx(Mix_Chunk* sfx) {
    if (sfx) Mix_PlayChannel(-1, sfx, 0);
}

std::vector<Tile> buildWall() {
    std::vector<Tile> deck;
    const char* zNames[] = {"东", "南", "西", "北", "中", "发", "白"};
    for (int s = 0; s < 3; ++s) {
        const char* sName = (s == 0) ? "万" : (s == 1 ? "筒" : "条");
        for (int v = 1; v <= 9; ++v) {
            for (int c = 0; c < 4; ++c) {
                deck.push_back({s, v, std::to_string(v) + sName});
            }
        }
    }
    for (int v = 1; v <= 7; ++v) {
        for (int c = 0; c < 4; ++c) {
            deck.push_back({3, v, zNames[v - 1]});
        }
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(deck.begin(), deck.end(), g);
    return deck;
}

bool solveMelds(std::map<int, int>& cnt) {
    int firstKey = -1;
    for (auto& kv : cnt) {
        if (kv.second > 0) { firstKey = kv.first; break; }
    }
    if (firstKey == -1) return true;

    if (cnt[firstKey] >= 3) {
        cnt[firstKey] -= 3;
        if (solveMelds(cnt)) return true;
        cnt[firstKey] += 3;
    }
    int suit = firstKey / 100;
    int val = firstKey % 100;
    if (suit < 3 && val <= 7) {
        int k2 = suit * 100 + (val + 1);
        int k3 = suit * 100 + (val + 2);
        if (cnt[k2] > 0 && cnt[k3] > 0) {
            cnt[firstKey]--; cnt[k2]--; cnt[k3]--;
            if (solveMelds(cnt)) return true;
            cnt[firstKey]++; cnt[k2]++; cnt[k3]++;
        }
    }
    return false;
}

bool canWin(const std::vector<Tile>& tiles) {
    if (tiles.size() < 2 || tiles.size() % 3 != 2) return false;
    std::map<int, int> cnt;
    for (const auto& t : tiles) cnt[t.suit * 100 + t.val]++;
    for (auto& kv : cnt) {
        if (kv.second >= 2) {
            kv.second -= 2;
            if (solveMelds(cnt)) return true;
            kv.second += 2;
        }
    }
    return false;
}

SDL_Color getTileColor(const Tile& t) {
    if (t.suit == 0) return {185, 28, 28, 255};
    if (t.suit == 1) return {29, 78, 216, 255};
    if (t.suit == 2) return {21, 128, 61, 255};
    if (t.val == 5)  return {185, 28, 28, 255};
    if (t.val == 6)  return {21, 128, 61, 255};
    return {30, 41, 59, 255};
}

void drawText(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    if (!font || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(ren, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

int main(int argc, char* argv[]) {
    romfsInit();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);

    SDL_Window* win = SDL_CreateWindow("Mahjong", 0, 0, 1280, 720, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    TTF_Font* fontBig = TTF_OpenFont("romfs:/font.ttf", 26);
    TTF_Font* fontSmall = TTF_OpenFont("romfs:/font.ttf", 20);

    gBgm = Mix_LoadMUS("romfs:/bgm.ogg");
    gSfxPlay = Mix_LoadWAV("romfs:/voices/play.wav");
    gSfxHu = Mix_LoadWAV("romfs:/voices/hu.wav");
    gSfxTaunt = Mix_LoadWAV("romfs:/voices/taunt.wav");
    if (gBgm) Mix_PlayMusic(gBgm, -1);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    std::vector<Tile> wall;
    std::vector<Tile> playerHand;
    std::vector<Tile> discards;
    std::vector<AIPlayer> ais = {
        {"暴躁大哥", "准备好被我横扫了吗？", {}, {"这你都不信！", "快点啦，磨磨蹭蹭的！", "乱打小心点炮哦！"}},
        {"甜美辣妹", "人家今天手气超好的啦", {}, {"哎呀，手滑打错了", "这张人家不要哦", "听牌啦听牌啦！"}},
        {"淡定军师", "不急，慢慢算牌。", {}, {"嗯，稳扎稳打。", "让我想想...", "这把牌面不错。"}}
    };

    int cursor = 0;
    int phase = 0;
    std::string statusMsg = "按 [R] 开始新局 | 左右选牌 | [A] 出牌 | [X] 摸牌 | [Y] 喊胡";

    auto startNewGame = [&]() {
        wall = buildWall();
        playerHand.clear();
        discards.clear();
        for (int i = 0; i < 3; ++i) ais[i].hand.clear();

        for (int r = 0; r < 16; ++r) {
            playerHand.push_back(wall.back()); wall.pop_back();
            for (int i = 0; i < 3; ++i) {
                ais[i].hand.push_back(wall.back()); wall.pop_back();
            }
        }
        playerHand.push_back(wall.back()); wall.pop_back();
        std::sort(playerHand.begin(), playerHand.end());
        cursor = 0;
        phase = 0;
        statusMsg = "你的回合：左右选牌，按 [A] 打出（按 [Y] 可检查胡牌）";
    };

    startNewGame();

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) break;
        if (kDown & HidNpadButton_R) startNewGame();

        if (phase == 0 && !playerHand.empty()) {
            if (kDown & HidNpadButton_Left) {
                cursor = (cursor - 1 + (int)playerHand.size()) % (int)playerHand.size();
            }
            if (kDown & HidNpadButton_Right) {
                cursor = (cursor + 1) % (int)playerHand.size();
            }
            if (kDown & HidNpadButton_A) {
                Tile out = playerHand[cursor];
                playerHand.erase(playerHand.begin() + cursor);
                discards.push_back(out);
                playSfx(gSfxPlay);
                if (cursor >= (int)playerHand.size()) cursor = std::max(0, (int)playerHand.size() - 1);

                for (int i = 0; i < 3; ++i) {
                    if (wall.empty()) break;
                    ais[i].hand.push_back(wall.back()); wall.pop_back();
                    int dropIdx = std::rand() % ais[i].hand.size();
                    discards.push_back(ais[i].hand[dropIdx]);
                    ais[i].hand.erase(ais[i].hand.begin() + dropIdx);
                    ais[i].bubble = ais[i].lines[std::rand() % ais[i].lines.size()];
                }
                playSfx(gSfxTaunt);

                if (wall.empty()) {
                    phase = 2;
                    statusMsg = "流局！牌堆已摸完，按 [R] 再来一局";
                } else {
                    phase = 1;
                    statusMsg = "轮到你了：请按 [X] 摸牌";
                }
            }
        } else if (phase == 1) {
            if (kDown & HidNpadButton_X) {
                if (!wall.empty()) {
                    playerHand.push_back(wall.back()); wall.pop_back();
                    std::sort(playerHand.begin(), playerHand.end());
                    playSfx(gSfxPlay);
                    phase = 0;
                    statusMsg = "已摸牌：按 [A] 出牌，或按 [Y] 喊胡！";
                }
            }
        }

        if (kDown & HidNpadButton_Y && phase != 2) {
            if (canWin(playerHand)) {
                playSfx(gSfxHu);
                phase = 2;
                statusMsg = "恭喜胡牌！这把你赢了！按 [R] 开始新局";
            } else {
                statusMsg = "还没凑齐胡牌牌型哦！请继续出牌";
            }
        }

        SDL_SetRenderDrawColor(ren, 20, 83, 45, 255);
        SDL_RenderClear(ren);

        SDL_Color cWhite = {255, 255, 255, 255};
        SDL_Color cYellow = {253, 224, 71, 255};
        SDL_Color cDark = {20, 20, 20, 255};

        drawText(ren, fontSmall, statusMsg, 40, 20, cYellow);
        drawText(ren, fontSmall, "剩余牌数: " + std::to_string(wall.size()) + "  |  [+] 退出", 960, 20, cWhite);

        for (int i = 0; i < 3; ++i) {
            SDL_Rect box = {40 + i * 410, 70, 380, 110};
            SDL_SetRenderDrawColor(ren, 15, 60, 32, 255);
            SDL_RenderFillRect(ren, &box);
            drawText(ren, fontSmall, ais[i].name + " (剩" + std::to_string(ais[i].hand.size()) + "张)", box.x + 15, box.y + 12, cWhite);

            SDL_Rect bub = {box.x + 15, box.y + 50, 350, 44};
            SDL_SetRenderDrawColor(ren, 254, 240, 138, 255);
            SDL_RenderFillRect(ren, &bub);
            drawText(ren, fontSmall, ais[i].bubble, bub.x + 10, bub.y + 10, cDark);
        }

        SDL_Rect river = {40, 205, 1200, 270};
        SDL_SetRenderDrawColor(ren, 17, 70, 38, 255);
        SDL_RenderFillRect(ren, &river);
        drawText(ren, fontSmall, "牌河（最近打出）：", 55, 215, cWhite);

        int startIdx = std::max(0, (int)discards.size() - 24);
        for (int i = startIdx; i < (int)discards.size(); ++i) {
            int idx = i - startIdx;
            int col = idx % 12;
            int row = idx / 12;
            SDL_Rect tRect = {60 + col * 96, 255 + row * 100, 84, 86};
            SDL_SetRenderDrawColor(ren, 245, 245, 220, 255);
            SDL_RenderFillRect(ren, &tRect);
            drawText(ren, fontBig, discards[i].label, tRect.x + 14, tRect.y + 24, getTileColor(discards[i]));
        }

        for (int i = 0; i < (int)playerHand.size(); ++i) {
            bool selected = (i == cursor && phase == 0);
            SDL_Rect card = {40 + i * 71, selected ? 515 : 545, 65, 110};
            SDL_SetRenderDrawColor(ren, selected ? 255 : 254, selected ? 250 : 252, selected ? 200 : 232, 255);
            SDL_RenderFillRect(ren, &card);
            drawText(ren, fontBig, playerHand[i].label, card.x + 8, card.y + 35, getTileColor(playerHand[i]));
        }

        SDL_RenderPresent(ren);
    }

    Mix_FreeChunk(gSfxPlay);
    Mix_FreeChunk(gSfxHu);
    Mix_FreeChunk(gSfxTaunt);
    Mix_FreeMusic(gBgm);
