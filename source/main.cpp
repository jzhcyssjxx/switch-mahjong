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
    int suit;
    int val;
    std::string label;
    bool operator==(const Tile& o) const { return suit == o.suit && val == o.val; }
    bool operator<(const Tile& o) const {
        if (suit != o.suit) return suit < o.suit;
        return val < o.val;
    }
};

struct Meld {
    int type;
    std::vector<Tile> tiles;
};

struct AIPlayer {
    std::string name;
    std::string bubble;
    std::vector<Tile> hand;
    std::vector<Meld> melds;
    std::vector<std::string> lines;
};

Mix_Music* gBgm = nullptr;
Mix_Chunk* gSfxPlay = nullptr;
Mix_Chunk* gSfxChi = nullptr;
Mix_Chunk* gSfxPeng = nullptr;
Mix_Chunk* gSfxGang = nullptr;
Mix_Chunk* gSfxHu = nullptr;
Mix_Chunk* gSfxTaunt = nullptr;

void playSfx(Mix_Chunk* sfx, Mix_Chunk* fallback = nullptr) {
    if (sfx) Mix_PlayChannel(-1, sfx, 0);
    else if (fallback) Mix_PlayChannel(-1, fallback, 0);
}

std::vector<Tile> buildWall() {
    std::vector<Tile> deck;
    std::vector<std::string> zNames = {"东", "南", "西", "北", "中", "发", "白"};
    for (int s = 0; s < 3; ++s) {
        std::string sName = (s == 0) ? "万" : (s == 1 ? "筒" : "条");
        for (int v = 1; v <= 9; ++v) {
            for (int c = 0; c < 4; ++c) deck.push_back({s, v, std::to_string(v) + sName});
        }
    }
    for (int v = 1; v <= 7; ++v) {
        for (int c = 0; c < 4; ++c) deck.push_back({3, v, zNames.at(v - 1)});
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

    if (cnt.at(firstKey) >= 3) {
        cnt.at(firstKey) -= 3;
        if (solveMelds(cnt)) return true;
        cnt.at(firstKey) += 3;
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

std::vector<Tile> getTenpaiTiles(const std::vector<Tile>& hand) {
    std::vector<Tile> waits;
    if (hand.size() % 3 != 1) return waits;
    std::vector<std::string> zNames = {"东", "南", "西", "北", "中", "发", "白"};
    for (int s = 0; s < 4; ++s) {
        int maxV = (s < 3) ? 9 : 7;
        for (int v = 1; v <= maxV; ++v) {
            std::string lbl = (s < 3) ? (std::to_string(v) + (s == 0 ? "万" : (s == 1 ? "筒" : "条"))) : zNames.at(v - 1);
            Tile cand{s, v, lbl};
            auto testHand = hand;
            testHand.push_back(cand);
            if (canWin(testHand)) waits.push_back(cand);
        }
    }
    return waits;
}

int countTile(const std::vector<Tile>& hand, const Tile& t) {
    int c = 0;
    for (const auto& x : hand) if (x == t) c++;
    return c;
}

bool hasTileVal(const std::vector<Tile>& hand, int suit, int val) {
    for (const auto& x : hand) if (x.suit == suit && x.val == val) return true;
    return false;
}

int pickAIDiscard(const std::vector<Tile>& hand) {
    int bestIdx = 0, lowestScore = 999;
    for (int i = 0; i < (int)hand.size(); ++i) {
        int score = 0;
        const Tile& t = hand.at(i);
        for (int j = 0; j < (int)hand.size(); ++j) {
            if (i == j) continue;
            if (hand.at(j) == t) score += 10;
            else if (t.suit < 3 && hand.at(j).suit == t.suit && std::abs(hand.at(j).val - t.val) == 1) score += 6;
            else if (t.suit < 3 && hand.at(j).suit == t.suit && std::abs(hand.at(j).val - t.val) == 2) score += 3;
        }
        if (t.suit == 3) score -= 2;
        if (score < lowestScore) { lowestScore = score; bestIdx = i; }
    }
    return bestIdx;
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
    std::srand((unsigned int)std::time(nullptr));
    romfsInit();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);

    SDL_Window* win = SDL_CreateWindow("Mahjong", 0, 0, 1280, 720, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    TTF_Font* fontBig = TTF_OpenFont("romfs:/font.ttf", 24);
    TTF_Font* fontSmall = TTF_OpenFont("romfs:/font.ttf", 19);

    gBgm = Mix_LoadMUS("romfs:/bgm.ogg");
    gSfxPlay = Mix_LoadWAV("romfs:/voices/play.wav");
    gSfxChi = Mix_LoadWAV("romfs:/voices/chi.wav");
    gSfxPeng = Mix_LoadWAV("romfs:/voices/peng.wav");
    gSfxGang = Mix_LoadWAV("romfs:/voices/gang.wav");
    gSfxHu = Mix_LoadWAV("romfs:/voices/hu.wav");
    gSfxTaunt = Mix_LoadWAV("romfs:/voices/taunt.wav");
    if (gBgm) Mix_PlayMusic(gBgm, -1);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    std::vector<Tile> wall;
    std::vector<Tile> playerHand;
    std::vector<Meld> playerMelds;
    std::vector<Tile> discards;

    std::vector<AIPlayer> ais = {
        {"下家・甜美辣妹", "人家今天手气超好的啦", {}, {}, {"哎呀，手滑打错了", "这张人家不要哦", "听牌啦听牌啦！"}},
        {"对家・淡定军师", "不急，慢慢算牌。", {}, {}, {"嗯，稳扎稳打。", "让我想想...", "这把牌面不错。"}},
        {"上家・暴躁大哥", "准备好被我横扫了吗？", {}, {}, {"这你都不信！", "快点啦，磨磨蹭蹭的！", "乱打小心点炮哦！"}}
    };

    int cursor = 0;
    int turn = 3;
    int phase = 0;
    Tile lastDiscard{0, 1, ""};
    int lastDiscardFrom = -1;

    bool optHu = false, optGang = false, optPeng = false;
    std::vector<std::pair<int, int>> optChis;
    std::string statusMsg = "";
    Uint32 aiTimer = 0;

    auto playerDrawAndEnterTurn = [&]() {
        if (wall.empty()) {
            phase = 3;
            statusMsg = "流局！牌堆已摸完，按 [R] 再来一局";
            return;
        }
        playerHand.push_back(wall.back());
        wall.pop_back();
        std::sort(playerHand.begin(), playerHand.end());
        cursor = std::max(0, (int)playerHand.size() - 1);
        turn = 3;
        phase = 0;
        statusMsg = "你的回合：左右选牌，[A] 出牌";
        if (canWin(playerHand)) statusMsg += " | 可按 [Y] 自摸胡牌！";
    };

    auto startNewGame = [&]() {
        wall = buildWall();
        playerHand.clear();
        playerMelds.clear();
        discards.clear();
        for (int i = 0; i < 3; ++i) {
            ais.at(i).hand.clear();
            ais.at(i).melds.clear();
        }
        for (int r = 0; r < 16; ++r) {
            playerHand.push_back(wall.back()); wall.pop_back();
            for (int i = 0; i < 3; ++i) { ais.at(i).hand.push_back(wall.back()); wall.pop_back(); }
        }
        playerHand.push_back(wall.back()); wall.pop_back();
        std::sort(playerHand.begin(), playerHand.end());
        cursor = 0;
        turn = 3;
        phase = 0;
        statusMsg = "新局开始！左右选牌，按 [A] 出牌";
        if (canWin(playerHand)) statusMsg += " | 天胡！按 [Y] 胡牌！";
    };

    auto checkPlayerReactions = [&](const Tile& t, int fromSeat) -> bool {
        optHu = false; optGang = false; optPeng = false; optChis.clear();

        auto testWin = playerHand;
        testWin.push_back(t);
        if (canWin(testWin)) optHu = true;

        int same = countTile(playerHand, t);
        if (same >= 3) optGang = true;
        if (same >= 2) optPeng = true;

        if (fromSeat == 2 && t.suit < 3) {
            int v = t.val;
            if (hasTileVal(playerHand, t.suit, v - 2) && hasTileVal(playerHand, t.suit, v - 1)) optChis.push_back({v - 2, v - 1});
            if (hasTileVal(playerHand, t.suit, v - 1) && hasTileVal(playerHand, t.suit, v + 1)) optChis.push_back({v - 1, v + 1});
            if (hasTileVal(playerHand, t.suit, v + 1) && hasTileVal(playerHand, t.suit, v + 2)) optChis.push_back({v + 1, v + 2});
        }

        if (optHu || optGang || optPeng || !optChis.empty()) {
            lastDiscard = t;
            lastDiscardFrom = fromSeat;
            phase = 2;
            statusMsg = "可抢牌！";
            if (optHu) statusMsg += " [Y]胡牌";
            if (optGang) statusMsg += " [L]明杠";
            if (optPeng) statusMsg += " [X]碰牌";
            if (!optChis.empty()) statusMsg += " [A]吃牌";
            statusMsg += " | [B]跳过";
            return true;
        }
        return false;
    };

    auto removeOneTile = [&](std::vector<Tile>& h, int suit, int val) {
        for (auto it = h.begin(); it != h.end(); ++it) {
            if (it->suit == suit && it->val == val) { h.erase(it); return; }
        }
    };

    startNewGame();

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) break;
        if (kDown & HidNpadButton_R) startNewGame();

        if (phase == 0 && !playerHand.empty()) {
            if (kDown & HidNpadButton_Left) cursor = (cursor - 1 + (int)playerHand.size()) % (int)playerHand.size();
            if (kDown & HidNpadButton_Right) cursor = (cursor + 1) % (int)playerHand.size();

            if ((kDown & HidNpadButton_Y) && canWin(playerHand)) {
                playSfx(gSfxHu);
                phase = 3;
                statusMsg = "自摸胡牌！太强了！按 [R] 再来一局";
            }
            else if (kDown & HidNpadButton_L) {
                bool didGang = false;
                for (const auto& t : playerHand) {
                    if (countTile(playerHand, t) == 4) {
                        for (int k = 0; k < 4; ++k) removeOneTile(playerHand, t.suit, t.val);
                        playerMelds.push_back({3, {t, t, t, t}});
                        didGang = true;
                        break;
                    }
                }
                if (!didGang) {
                    for (auto& m : playerMelds) {
                        Tile firstTile = m.tiles.at(0);
                        if (m.type == 1 && hasTileVal(playerHand, firstTile.suit, firstTile.val)) {
                            removeOneTile(playerHand, firstTile.suit, firstTile.val);
                            m.type = 2;
                            m.tiles.push_back(firstTile);
                            didGang = true;
                            break;
                        }
                    }
                }
                if (didGang) {
                    playSfx(gSfxGang, gSfxPlay);
                    playerDrawAndEnterTurn();
                    statusMsg = "杠牌并补牌成功！请选择一张打出";
                }
            }
            else if (kDown & HidNpadButton_A) {
                Tile out = playerHand.at(cursor);
                playerHand.erase(playerHand.begin() + cursor);
                discards.push_back(out);
                playSfx(gSfxPlay);
                if (cursor >= (int)playerHand.size()) cursor = std::max(0, (int)playerHand.size() - 1);

                turn = 0;
                phase = 1;
                aiTimer = SDL_GetTicks();
                statusMsg = "等待牌友出牌...";
            }
        }
        else if (phase == 1) {
            if (SDL_GetTicks() - aiTimer >= 450) {
                if (wall.empty()) {
                    phase = 3;
                    statusMsg = "流局！牌堆已摸完，按 [R] 再来一局";
                } else {
                    AIPlayer& ai = ais.at(turn);
                    ai.hand.push_back(wall.back());
                    wall.pop_back();

                    int dropIdx = pickAIDiscard(ai.hand);
                    Tile out = ai.hand.at(dropIdx);
                    ai.hand.erase(ai.hand.begin() + dropIdx);
                    discards.push_back(out);
                    ai.bubble = ai.lines.at(std::rand() % ai.lines.size());
                    playSfx(gSfxTaunt, gSfxPlay);

                    int whoDiscarded = turn;
                    turn = (turn + 1) % 4;

                    if (!checkPlayerReactions(out, whoDiscarded)) {
                        if (turn == 3) {
                            playerDrawAndEnterTurn();
                        } else {
                            aiTimer = SDL_GetTicks();
                        }
                    }
                }
            }
        }
        else if (phase == 2) {
            if ((kDown & HidNpadButton_Y) && optHu) {
                playerHand.push_back(lastDiscard);
                std::sort(playerHand.begin(), playerHand.end());
                playSfx(gSfxHu);
                phase = 3;
                statusMsg = "抢胡点炮成功！你赢了！按 [R] 再来一局";
            } else if ((kDown & HidNpadButton_L) && optGang) {
                discards.pop_back();
                for (int k = 0; k < 3; ++k) removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                playerMelds.push_back({2, {lastDiscard, lastDiscard, lastDiscard, lastDiscard}});
                playSfx(gSfxGang, gSfxPlay);
                playerDrawAndEnterTurn();
                statusMsg = "明杠成功并补牌！请打出一张牌";
            } else if ((kDown & HidNpadButton_X) && optPeng) {
                discards.pop_back();
                removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                playerMelds.push_back({1, {lastDiscard, lastDiscard, lastDiscard}});
                playSfx(gSfxPeng, gSfxPlay);
                std::sort(playerHand.begin(), playerHand.end());
                cursor = 0;
                turn = 3;
                phase = 0;
                statusMsg = "碰牌成功！请选择一张牌打出";
            } else if ((kDown & HidNpadButton_A) && !optChis.empty()) {
                discards.pop_back();
                auto pairV = optChis.at(0);
                removeOneTile(playerHand, lastDiscard.suit, pairV.first);
                removeOneTile(playerHand, lastDiscard.suit, pairV.second);
                std::string sName = (lastDiscard.suit == 0) ? "万" : (lastDiscard.suit == 1 ? "筒" : "条");
                std::vector<Tile> meldTiles = {
                    {lastDiscard.suit, pairV.first, std::to_string(pairV.first) + sName},
                    lastDiscard,
                    {lastDiscard.suit, pairV.second, std::to_string(pairV.second) + sName}
                };
                std::sort(meldTiles.begin(), meldTiles.end());
                playerMelds.push_back({0, meldTiles});
                playSfx(gSfxChi, gSfxPlay);
                std::sort(playerHand.begin(), playerHand.end());
                cursor = 0;
                turn = 3;
                phase = 0;
                statusMsg = "吃牌成功！请选择一张牌打出";
            } else if (kDown & HidNpadButton_B) {
                if (turn == 3) {
                    playerDrawAndEnterTurn();
                } else {
                    phase = 1;
                    aiTimer = SDL_GetTicks();
                    statusMsg = "等待牌友出牌...";
                }
            }
        }

        SDL_SetRenderDrawColor(ren, 20, 83, 45, 255);
        SDL_RenderClear(ren);

        SDL_Color cWhite = {255, 255, 255, 255};
        SDL_Color cYellow = {253, 224, 71, 255};
        SDL_Color cDark = {20, 20, 20, 255};
        SDL_Color cCyan = {103, 232, 249, 255};

        drawText(ren, fontSmall, statusMsg, 30, 16, cYellow);
        drawText(ren, fontSmall, "余牌: " + std::to_string(wall.size()) + " | [R]重开 [+]退出", 960, 16, cWhite);

        std::vector<int> displayOrder = {2, 1, 0};
        for (int idx = 0; idx < 3; ++idx) {
            int aiIdx = displayOrder.at(idx);
            SDL_Rect box = {30 + idx * 415, 55, 390, 100};
            SDL_SetRenderDrawColor(ren, 15, 60, 32, 255);
            SDL_RenderFillRect(ren, &box);
            drawText(ren, fontSmall, ais.at(aiIdx).name + " (手牌" + std::to_string(ais.at(aiIdx).hand.size()) + "张)", box.x + 12, box.y + 10, cWhite);

            SDL_Rect bub = {box.x + 12, box.y + 44, 366, 42};
            SDL_SetRenderDrawColor(ren, 254, 240, 138, 255);
            SDL_RenderFillRect(ren, &bub);
            drawText(ren, fontSmall, ais.at(aiIdx).bubble, bub.x + 10, bub.y + 9, cDark);
        }
        SDL_Rect river = {30, 175, 1220, 250};
        SDL_SetRenderDrawColor(ren, 17, 70, 38, 255);
        SDL_RenderFillRect(ren, &river);
        drawText(ren, fontSmall, "牌河（最近打出）：", 45, 183, cWhite);

        int startIdx = std::max(0, (int)discards.size() - 28);
        for (int i = startIdx; i < (int)discards.size(); ++i) {
            int idx = i - startIdx;
            int col = idx % 14;
            int row = idx / 14;
            SDL_Rect tRect = {45 + col * 85, 218 + row * 96, 76, 84};
            SDL_SetRenderDrawColor(ren, 245, 245, 220, 255);
            SDL_RenderFillRect(ren, &tRect);
            drawText(ren, fontBig, discards.at(i).label, tRect.x + 10, tRect.y + 24, getTileColor(discards.at(i)));
        }

        if (phase == 2) {
            SDL_Rect banner = {220, 260, 840, 90};
            SDL_SetRenderDrawColor(ren, 127, 29, 29, 240);
            SDL_RenderFillRect(ren, &banner);
            drawText(ren, fontBig, "打出【" + lastDiscard.label + "】！ " + statusMsg, banner.x + 30, banner.y + 28, cYellow);
        }

        std::string infoLine = "";
        if (phase == 0) {
            bool canSelfGang = false;
            for (const auto& t : playerHand) if (countTile(playerHand, t) == 4) canSelfGang = true;
            for (const auto& m : playerMelds) {
                Tile firstTile = m.tiles.at(0);
                if (m.type == 1 && hasTileVal(playerHand, firstTile.suit, firstTile.val)) canSelfGang = true;
            }
            if (canSelfGang) infoLine += "[提示：手中有杠牌，可按 L 键开杠]   ";
        } else {
            auto waits = getTenpaiTiles(playerHand);
            if (!waits.empty()) {
                infoLine += "【已听牌！胡：";
                for (size_t i = 0; i < waits.size(); ++i) infoLine += waits.at(i).label + (i + 1 < waits.size() ? "、" : "");
                infoLine += "】";
            }
        }
        if (!infoLine.empty()) drawText(ren, fontSmall, infoLine, 35, 440, cCyan);

        int drawX = 30;
        for (const auto& m : playerMelds) {
            for (size_t k = 0; k < m.tiles.size(); ++k) {
                bool hidden = (m.type == 3 && (k == 0 || k == 3));
                SDL_Rect mRect = {drawX, 555, 50, 85};
                SDL_SetRenderDrawColor(ren, hidden ? 30 : 230, hidden ? 100 : 230, hidden ? 60 : 210, 255);
                SDL_RenderFillRect(ren, &mRect);
                if (!hidden) drawText(ren, fontSmall, m.tiles.at(k).label, mRect.x + 4, mRect.y + 28, getTileColor(m.tiles.at(k)));
                drawX += 53;
            }
            drawX += 12;
        }

        int cardW = std::min(64, (1240 - drawX) / std::max(1, (int)playerHand.size()) - 4);
        for (int i = 0; i < (int)playerHand.size(); ++i) {
            bool selected = (i == cursor && phase == 0);
            SDL_Rect card = {drawX + i * (cardW + 4), selected ? 505 : 535, cardW, 105};
            SDL_SetRenderDrawColor(ren, selected ? 255 : 254, selected ? 250 : 252, selected ? 200 : 232, 255);
            SDL_RenderFillRect(ren, &card);
            drawText(ren, fontBig, playerHand.at(i).label, card.x + 5, card.y + 34, getTileColor(playerHand.at(i)));
        }

        SDL_RenderPresent(ren);
    }

    Mix_FreeChunk(gSfxPlay);
    Mix_FreeChunk(gSfxChi);
    Mix_FreeChunk(gSfxPeng);
    Mix_FreeChunk(gSfxGang);
    Mix_FreeChunk(gSfxHu);
    Mix_FreeChunk(gSfxTaunt);
    Mix_FreeMusic(gBgm);
    if (fontBig) TTF_CloseFont(fontBig);
    if (fontSmall) TTF_CloseFont(fontSmall);
    Mix_CloseAudio();
    TTF_Quit();
    SDL_Quit();
    romfsExit();
    return 0;
}
