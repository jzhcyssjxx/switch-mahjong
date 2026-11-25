#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdio>
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
    bool operator<(const Tile& o) const { return (suit != o.suit) ? (suit < o.suit) : (val < o.val); }
};

struct Meld {
    int type;
    std::vector<Tile> tiles;
};

struct AIPlayer {
    std::string name;
    std::string bubble;
    int score;
    std::vector<Tile> hand;
    std::vector<Meld> melds;
    std::vector<std::string> lines;
};

struct WinResult {
    int winnerSeat;
    std::string title;
    std::string details;
    std::vector<int> scoreDeltas;
};

std::vector<Mix_Music*> gBgmList;
int gCurrentBgm = -1;
Mix_Chunk* gSfxPlay = nullptr;
Mix_Chunk* gSfxChi = nullptr;
Mix_Chunk* gSfxPeng = nullptr;
Mix_Chunk* gSfxGang = nullptr;
Mix_Chunk* gSfxHu = nullptr;
Mix_Chunk* gSfxTaunt = nullptr;

void playRandomBgm() {
    if (gBgmList.empty()) return;
    int nextIdx = std::rand() % (int)gBgmList.size();
    if (gBgmList.size() > 1 && nextIdx == gCurrentBgm) nextIdx = (nextIdx + 1) % (int)gBgmList.size();
    gCurrentBgm = nextIdx;
    Mix_VolumeMusic(56);
    Mix_PlayMusic(gBgmList.at(gCurrentBgm), 1);
}

void playSfx(Mix_Chunk* sfx, Mix_Chunk* fallback = nullptr) {
    if (sfx) Mix_PlayChannel(-1, sfx, 0);
    else if (fallback) Mix_PlayChannel(-1, fallback, 0);
}

std::string makeTileLabel(int suit, int val) {
    std::vector<std::string> zNames = {"东", "南", "西", "北", "中", "发", "白"};
    if (suit == 0) return std::to_string(val) + "万";
    if (suit == 1) return std::to_string(val) + "筒";
    if (suit == 2) return std::to_string(val) + "条";
    return zNames.at(val - 1);
}
Tile getNextDoraTile(const Tile& ind) {
    if (ind.suit < 3) {
        int nv = (ind.val % 9) + 1;
        return {ind.suit, nv, makeTileLabel(ind.suit, nv)};
    } else if (ind.val <= 4) {
        int nv = (ind.val % 4) + 1;
        return {3, nv, makeTileLabel(3, nv)};
    } else {
        int nv = ((ind.val - 5 + 1) % 3) + 5;
        return {3, nv, makeTileLabel(3, nv)};
    }
}

std::vector<Tile> buildWall() {
    std::vector<Tile> deck;
    for (int s = 0; s < 3; ++s) {
        for (int v = 1; v <= 9; ++v) {
            for (int c = 0; c < 4; ++c) deck.push_back({s, v, makeTileLabel(s, v)});
        }
    }
    for (int v = 1; v <= 7; ++v) {
        for (int c = 0; c < 4; ++c) deck.push_back({3, v, makeTileLabel(3, v)});
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(deck.begin(), deck.end(), g);
    return deck;
}

int getMapVal(const std::map<int, int>& m, int key) {
    auto it = m.find(key);
    return (it != m.end()) ? it->second : 0;
}

bool solveMelds(std::map<int, int>& cnt, bool allowChow) {
    int firstKey = -1;
    for (auto& kv : cnt) {
        if (kv.second > 0) { firstKey = kv.first; break; }
    }
    if (firstKey == -1) return true;

    if (cnt.at(firstKey) >= 3) {
        cnt.at(firstKey) -= 3;
        if (solveMelds(cnt, allowChow)) return true;
        cnt.at(firstKey) += 3;
    }
    if (allowChow) {
        int suit = firstKey / 100;
        int val = firstKey % 100;
        if (suit < 3 && val <= 7) {
            int k2 = suit * 100 + (val + 1);
            int k3 = suit * 100 + (val + 2);
            if (getMapVal(cnt, k2) > 0 && getMapVal(cnt, k3) > 0) {
                cnt.at(firstKey)--; cnt.at(k2)--; cnt.at(k3)--;
                if (solveMelds(cnt, allowChow)) return true;
                cnt.at(firstKey)++; cnt.at(k2)++; cnt.at(k3)++;
            }
        }
    }
    return false;
}

bool isSevenPairs(const std::vector<Tile>& tiles) {
    if (tiles.size() != 14) return false;
    std::map<int, int> cnt;
    for (const auto& t : tiles) cnt[t.suit * 100 + t.val]++;
    if (cnt.size() != 7) return false;
    for (const auto& kv : cnt) if (kv.second != 2) return false;
    return true;
}

bool canWinShape(const std::vector<Tile>& tiles, bool isRiichiMode, bool allowChow = true) {
    if (isRiichiMode && allowChow && isSevenPairs(tiles)) return true;
    if (tiles.size() < 2 || tiles.size() % 3 != 2) return false;
    std::map<int, int> cnt;
    for (const auto& t : tiles) cnt[t.suit * 100 + t.val]++;
    for (auto& kv : cnt) {
        if (kv.second >= 2) {
            kv.second -= 2;
            if (solveMelds(cnt, allowChow)) return true;
            kv.second += 2;
        }
    }
    return false;
}
void analyzeTraits(const std::vector<Tile>& hand, const std::vector<Meld>& melds,
                   bool& hasDragon, bool& isTanyao, bool& isFlush, bool& isHalfFlush) {
    std::map<int, int> cnt;
    std::vector<bool> suitUsed = {false, false, false, false};
    isTanyao = true;
    for (const auto& t : hand) {
        cnt[t.suit * 100 + t.val]++;
        suitUsed.at(t.suit) = true;
        if (t.suit == 3 || t.val == 1 || t.val == 9) isTanyao = false;
    }
    for (const auto& m : melds) {
        for (const auto& t : m.tiles) {
            cnt[t.suit * 100 + t.val]++;
            suitUsed.at(t.suit) = true;
            if (t.suit == 3 || t.val == 1 || t.val == 9) isTanyao = false;
        }
    }
    hasDragon = (getMapVal(cnt, 305) >= 3 || getMapVal(cnt, 306) >= 3 || getMapVal(cnt, 307) >= 3);
    int numSuits = (suitUsed.at(0) ? 1 : 0) + (suitUsed.at(1) ? 1 : 0) + (suitUsed.at(2) ? 1 : 0);
    isFlush = (numSuits == 1 && !suitUsed.at(3));
    isHalfFlush = (numSuits == 1 && suitUsed.at(3));
}

WinResult evaluateWinFull(int winnerSeat, const std::string& winnerName, const std::vector<Tile>& hand, const std::vector<Meld>& melds,
                          bool isRiichiMode, bool isSelfDraw, bool isGangBloom, bool declaredRiichi, bool isIppatsu,
                          const Tile& doraTile, int loserSeat, int streakCount) {
    WinResult res;
    res.winnerSeat = winnerSeat;
    res.scoreDeltas = {0, 0, 0, 0};

    bool menQing = true, hasChowMeld = false;
    for (const auto& m : melds) {
        if (m.type != 3) menQing = false;
        if (m.type == 0) hasChowMeld = true;
    }
    bool hasDragon = false, isTanyao = false, isFlush = false, isHalfFlush = false;
    analyzeTraits(hand, melds, hasDragon, isTanyao, isFlush, isHalfFlush);
    bool isToitoi = (!hasChowMeld && canWinShape(hand, false, false));
    bool isChiitoi = (isRiichiMode && melds.empty() && isSevenPairs(hand));

    if (!isRiichiMode) {
        int tai = 1;
        std::string desc = "底台(1台)";
        if (streakCount > 0 && winnerSeat == 3) {
            int st = streakCount * 2 + 1;
            tai += st;
            desc += " + 连庄(" + std::to_string(st) + "台)";
        }
        if (menQing) { tai += 1; desc += " + 门清(1台)"; }
        if (isSelfDraw) {
            tai += 1; desc += " + 自摸(1台)";
            if (isGangBloom) { tai += 2; desc += " + 杠上开花(2台)"; }
        }
        if (hasDragon) { tai += 1; desc += " + 三元刻(1台)"; }
        if (isToitoi) { tai += 4; desc += " + 碰碰胡(4台)"; }
        if (isFlush) { tai += 8; desc += " + 清一色(8台)"; }
        else if (isHalfFlush) { tai += 4; desc += " + 混一色(4台)"; }

        int unit = 300 + tai * 200;
        if (isSelfDraw) {
            res.title = "(台麻) " + winnerName + " 自摸胡牌！共 " + std::to_string(tai) + " 台";
            for (int i = 0; i < 4; ++i) if (i != winnerSeat) res.scoreDeltas.at(i) = -unit;
            res.scoreDeltas.at(winnerSeat) = unit * 3;
        } else {
            int pay = unit * 2;
            res.title = "(台麻) " + winnerName + " 点炮胡牌！共 " + std::to_string(tai) + " 台";
            res.scoreDeltas.at(loserSeat) = -pay;
            res.scoreDeltas.at(winnerSeat) = pay;
        }
        res.details = desc;
    }
    else {
        int han = 0;
        std::string desc = "";
        auto addYaku = [&](const std::string& yName, int h) {
            if (!desc.empty()) desc += " + ";
            desc += yName + "(" + std::to_string(h) + "番)";
            han += h;
        };
        if (declaredRiichi) {
            addYaku("立直", 1);
            if (isIppatsu) addYaku("一发", 1);
        }
        if (menQing && isSelfDraw) addYaku("门前清自摸", 1);
        if (isGangBloom) addYaku("岭上开花", 1);
        if (isTanyao) addYaku("断幺九", 1);
        if (hasDragon) addYaku("役牌三元", 1);
        if (isChiitoi) addYaku("七对子", 2);
        if (isToitoi && !isChiitoi) addYaku("对对胡", 2);
        if (isHalfFlush) addYaku("混一色", menQing ? 3 : 2);
        if (isFlush) addYaku("清一色", menQing ? 6 : 5);
        if (han == 0) addYaku("平和保底", 1);

        int doraCnt = 0;
        for (const auto& t : hand) if (t == doraTile) doraCnt++;
        for (const auto& m : melds) for (const auto& t : m.tiles) if (t == doraTile) doraCnt++;
        if (doraCnt > 0) addYaku("宝牌(" + doraTile.label + ")", doraCnt);

        int totalPay = 1000 * (1 << std::min(han - 1, 4));
        std::string rankName = std::to_string(han) + "番";
        if (han >= 13) { totalPay = 32000; rankName = "累计役满(" + std::to_string(han) + "番)"; }
        else if (han >= 11) { totalPay = 24000; rankName = "三倍满(" + std::to_string(han) + "番)"; }
        else if (han >= 8)  { totalPay = 16000; rankName = "倍满(" + std::to_string(han) + "番)"; }
        else if (han >= 6)  { totalPay = 12000; rankName = "跳满(" + std::to_string(han) + "番)"; }
        else if (han == 5)  { totalPay = 8000;  rankName = "满贯(5番)"; }

        if (isSelfDraw) {
            int eachPay = (totalPay / 3 / 100) * 100;
            res.title = "(日麻) " + winnerName + " 自摸！ " + rankName;
            for (int i = 0; i < 4; ++i) if (i != winnerSeat) res.scoreDeltas.at(i) = -eachPay;
            res.scoreDeltas.at(winnerSeat) = eachPay * 3;
        } else {
            res.title = "(日麻) " + winnerName + " 荣和(点炮)！ " + rankName;
            res.scoreDeltas.at(loserSeat) = -totalPay;
            res.scoreDeltas.at(winnerSeat) = totalPay;
        }
        res.details = desc;
    }
    return res;
}
bool checkHasValidRiichiYaku(const std::vector<Tile>& hand, const std::vector<Meld>& melds, bool isSelfDraw, bool declaredRiichi) {
    if (declaredRiichi || isSelfDraw) return true;
    bool menQing = true, hasChow = false;
    for (const auto& m : melds) {
        if (m.type != 3) menQing = false;
        if (m.type == 0) hasChow = true;
    }
    if (menQing && isSevenPairs(hand)) return true;
    bool hasDragon = false, isTanyao = false, isFlush = false, isHalfFlush = false;
    analyzeTraits(hand, melds, hasDragon, isTanyao, isFlush, isHalfFlush);
    if (hasDragon || isTanyao || isFlush || isHalfFlush) return true;
    if (!hasChow && canWinShape(hand, false, false)) return true;
    return false;
}

std::vector<Tile> getTenpaiTiles(const std::vector<Tile>& hand, bool isRiichiMode) {
    std::vector<Tile> waits;
    if (hand.size() % 3 != 1) return waits;
    for (int s = 0; s < 4; ++s) {
        int maxV = (s < 3) ? 9 : 7;
        for (int v = 1; v <= maxV; ++v) {
            Tile cand{s, v, makeTileLabel(s, v)};
            auto testHand = hand;
            testHand.push_back(cand);
            if (canWinShape(testHand, isRiichiMode, true)) waits.push_back(cand);
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
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}
int main(int argc, char* argv[]) {
    FILE* fLog = fopen("sdmc:/mahjong_log.txt", "w");
    auto logStep = [&](const char* msg) {
        if (fLog) { fprintf(fLog, "%s\n", msg); fflush(fLog); }
    };
    logStep("1: start main");

    std::srand((unsigned int)std::time(nullptr));
    romfsInit();
    logStep("2: romfs ok");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    logStep("3: pad ok");

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    logStep("4: sdl libs ok");

    SDL_Window* win = SDL_CreateWindow("Mahjong", 0, 0, 1280, 720, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    logStep("5: renderer ok");

    TTF_Font* fontTitle = TTF_OpenFont("romfs:/font.ttf", 34);
    TTF_Font* fontBig = TTF_OpenFont("romfs:/font.ttf", 24);
    TTF_Font* fontSmall = TTF_OpenFont("romfs:/font.ttf", 19);
    logStep("6: font ok");

    std::vector<std::string> bgmPaths = {
        "romfs:/bgm.ogg", "romfs:/bgm1.ogg", "romfs:/bgm2.ogg",
        "romfs:/bgm3.ogg", "romfs:/bgm4.ogg", "romfs:/bgm5.ogg"
    };
    for (const auto& path : bgmPaths) {
        Mix_Music* m = Mix_LoadMUS(path.c_str());
        if (m) gBgmList.push_back(m);
    }
    playRandomBgm();

    gSfxPlay = Mix_LoadWAV("romfs:/voices/play.wav");
    gSfxChi = Mix_LoadWAV("romfs:/voices/chi.wav");
    gSfxPeng = Mix_LoadWAV("romfs:/voices/peng.wav");
    gSfxGang = Mix_LoadWAV("romfs:/voices/gang.wav");
    gSfxHu = Mix_LoadWAV("romfs:/voices/hu.wav");
    gSfxTaunt = Mix_LoadWAV("romfs:/voices/taunt.wav");
    logStep("7: audio ok");

    std::vector<SDL_Texture*> aiNormal = {
        IMG_LoadTexture(ren, "romfs:/ai0.png"),
        IMG_LoadTexture(ren, "romfs:/ai1.png"),
        IMG_LoadTexture(ren, "romfs:/ai2.png")
    };
    std::vector<SDL_Texture*> aiWin = {
        IMG_LoadTexture(ren, "romfs:/ai0_win.png"),
        IMG_LoadTexture(ren, "romfs:/ai1_win.png"),
        IMG_LoadTexture(ren, "romfs:/ai2_win.png")
    };
    std::vector<SDL_Texture*> aiLose = {
        IMG_LoadTexture(ren, "romfs:/ai0_lose.png"),
        IMG_LoadTexture(ren, "romfs:/ai1_lose.png"),
        IMG_LoadTexture(ren, "romfs:/ai2_lose.png")
    };
    logStep("8: avatars ok");

    bool inMenu = true;
    int menuCursor = 0;
    bool isRiichiMode = false;

    std::vector<Tile> wall;
    std::vector<Tile> playerHand;
    std::vector<Meld> playerMelds;
    std::vector<Tile> discards;
    Tile doraIndicator{0, 1, "1万"};
    Tile actualDora{0, 2, "2万"};

    int playerScore = 10000;
    int roundNum = 1;
    int streakCount = 0;

    std::vector<AIPlayer> ais = {
        {"下家・甜美辣妹", "人家今天手气超好的啦", 10000, {}, {}, {"哎呀，手滑打错了", "这张人家不要哦", "听牌啦听牌啦！"}},
        {"对家・淡定军师", "不急，慢慢算牌。", 10000, {}, {}, {"嗯，稳扎稳打。", "让我想想...", "这把牌面不错。"}},
        {"上家・暴躁大哥", "准备好被我横扫了吗？", 10000, {}, {}, {"这你都不信！", "快点啦，磨磨蹭蹭的！", "乱打小心点炮哦！"}}
    };
    int cursor = 0, turn = 3, phase = 0;
    Tile lastDiscard{0, 1, ""};
    int lastDiscardFrom = -1;
    bool justDidGang = false, playerRiichiDeclared = false, playerIppatsu = false, hasWinPanel = false;
    WinResult lastWinResult;
    bool optHu = false, optGang = false, optPeng = false;
    std::vector<std::pair<int, int>> optChis;
    std::string statusMsg = "";
    Uint32 aiTimer = 0;

    auto applyWinResult = [&](const WinResult& res) {
        lastWinResult = res;
        for (int i = 0; i < 3; ++i) ais.at(i).score += res.scoreDeltas.at(i);
        playerScore += res.scoreDeltas.at(3);
        streakCount = (res.winnerSeat == 3) ? (streakCount + 1) : 0;
        hasWinPanel = true;
        phase = 3;
        statusMsg = "本局结束！按(R)下一局 | (L+R)重置筹码 | (-)切换玩法";
    };

    auto playerDrawAndEnterTurn = [&](bool fromGang) {
        if (wall.empty()) {
            phase = 3;
            hasWinPanel = false;
            statusMsg = "流局！牌堆已摸完，按(R)继续下一局";
            return;
        }
        playerHand.push_back(wall.back());
        wall.pop_back();
        if (!playerRiichiDeclared) std::sort(playerHand.begin(), playerHand.end());
        cursor = std::max(0, (int)playerHand.size() - 1);
        turn = 3;
        phase = 0;
        justDidGang = fromGang;
        if (playerRiichiDeclared) {
            statusMsg = "(立直中) 自动摸牌：按(A)摸切打出";
            if (canWinShape(playerHand, true, true)) statusMsg += " | 按(Y)立直自摸！";
        } else {
            statusMsg = "你的回合：左右选牌，(A)出牌";
            if (canWinShape(playerHand, isRiichiMode, true)) statusMsg += " | 按(Y)自摸胡牌！";
        }
    };

    auto startNewGame = [&](bool resetAll) {
        int startChips = isRiichiMode ? 25000 : 10000;
        int initTiles = isRiichiMode ? 13 : 16;
        if (resetAll) {
            playerScore = startChips;
            roundNum = 1;
            streakCount = 0;
            for (int i = 0; i < 3; ++i) ais.at(i).score = startChips;
        } else {
            roundNum++;
        }
        wall = buildWall();
        if (isRiichiMode) {
            doraIndicator = wall.back();
            wall.pop_back();
            actualDora = getNextDoraTile(doraIndicator);
        }
        playerHand.clear();
        playerMelds.clear();
        discards.clear();
        hasWinPanel = false;
        justDidGang = false;
        playerRiichiDeclared = false;
        playerIppatsu = false;

        for (int i = 0; i < 3; ++i) {
            ais.at(i).hand.clear();
            ais.at(i).melds.clear();
        }
        for (int r = 0; r < initTiles; ++r) {
            playerHand.push_back(wall.back()); wall.pop_back();
            for (int i = 0; i < 3; ++i) { ais.at(i).hand.push_back(wall.back()); wall.pop_back(); }
        }
        playerHand.push_back(wall.back()); wall.pop_back();
        std::sort(playerHand.begin(), playerHand.end());
        cursor = 0;
        turn = 3;
        phase = 0;
        statusMsg = isRiichiMode ? "(日本立直麻将) 选牌按(A)出牌" : "(台湾16张麻将) 选牌按(A)出牌";
        if (canWinShape(playerHand, isRiichiMode, true)) statusMsg += " | 天胡！按(Y)胡牌！";
    };

    auto checkPlayerReactions = [&](const Tile& t, int fromSeat) -> bool {
        optHu = false; optGang = false; optPeng = false; optChis.clear();
        auto testWin = playerHand;
        testWin.push_back(t);
        if (canWinShape(testWin, isRiichiMode, true)) {
            if (!isRiichiMode || checkHasValidRiichiYaku(testWin, playerMelds, false, playerRiichiDeclared)) optHu = true;
        }
        if (!playerRiichiDeclared) {
            int same = countTile(playerHand, t);
            if (same >= 3) optGang = true;
            if (same >= 2) optPeng = true;
            if (fromSeat == 2 && t.suit < 3) {
                int v = t.val;
                if (hasTileVal(playerHand, t.suit, v - 2) && hasTileVal(playerHand, t.suit, v - 1)) optChis.push_back({v - 2, v - 1});
                if (hasTileVal(playerHand, t.suit, v - 1) && hasTileVal(playerHand, t.suit, v + 1)) optChis.push_back({v - 1, v + 1});
                if (hasTileVal(playerHand, t.suit, v + 1) && hasTileVal(playerHand, t.suit, v + 2)) optChis.push_back({v + 1, v + 2});
            }
        }
        if (optHu || optGang || optPeng || !optChis.empty()) {
            lastDiscard = t;
            lastDiscardFrom = fromSeat;
            phase = 2;
            statusMsg = "可抢牌！";
            if (optHu) statusMsg += " (Y)胡牌";
            if (optGang) statusMsg += " (L)明杠";
            if (optPeng) statusMsg += " (X)碰牌";
            if (!optChis.empty()) statusMsg += " (A)吃牌";
            statusMsg += " | (B)跳过";
            return true;
        }
        return false;
    };

    auto removeOneTile = [&](std::vector<Tile>& h, int suit, int val) {
        for (auto it = h.begin(); it != h.end(); ++it) {
            if (it->suit == suit && it->val == val) { h.erase(it); return; }
        }
    };
    logStep("9: entering main loop");
    bool loggedFirstFrame = false;

    while (appletMainLoop()) {
        if (!loggedFirstFrame) { logStep("10: loop running"); loggedFirstFrame = true; }
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);

        if (kDown & HidNpadButton_Plus) break;
        if (!gBgmList.empty() && (!Mix_PlayingMusic() || (kDown & HidNpadButton_ZL))) playRandomBgm();

        if (inMenu) {
            if (kDown & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right)) {
                menuCursor = 1 - menuCursor;
                playSfx(gSfxPlay);
            }
            if (kDown & HidNpadButton_A) {
                isRiichiMode = (menuCursor == 1);
                inMenu = false;
                playSfx(gSfxPeng, gSfxPlay);
                startNewGame(true);
            }

            SDL_SetRenderDrawColor(ren, 15, 55, 32, 255);
            SDL_RenderClear(ren);
            SDL_Color cGold = {251, 191, 36, 255}, cWhite = {255, 255, 255, 255}, cDark = {20, 20, 20, 255};

            drawText(ren, fontTitle, "明星三缺一风格・全能麻将", 410, 85, cGold);
            drawText(ren, fontSmall, "方向键选择玩法，按(A)开始 | 按(ZL)切换背景音乐 | 局中按(-)返回大厅", 270, 150, cWhite);

            SDL_Rect card0 = {180, 225, 420, 320};
            SDL_SetRenderDrawColor(ren, menuCursor == 0 ? 254 : 20, menuCursor == 0 ? 240 : 83, menuCursor == 0 ? 138 : 45, 255);
            SDL_RenderFillRect(ren, &card0);
            drawText(ren, fontBig, "模式一：台湾 16 张麻将", card0.x + 65, card0.y + 35, menuCursor == 0 ? cDark : cGold);
            drawText(ren, fontSmall, "・起手 16 张牌，凑 5 组顺刻 + 1 对眼", card0.x + 30, card0.y + 100, menuCursor == 0 ? cDark : cWhite);
            drawText(ren, fontSmall, "・经典台数制：底台、门清、自摸、连庄", card0.x + 30, card0.y + 140, menuCursor == 0 ? cDark : cWhite);
            drawText(ren, fontSmall, "・支持碰碰胡(4台)、清一色(8台)、杠开花", card0.x + 30, card0.y + 180, menuCursor == 0 ? cDark : cWhite);
            drawText(ren, fontSmall, "・初始筹码：10,000 分", card0.x + 30, card0.y + 240, menuCursor == 0 ? cDark : cGold);

            SDL_Rect card1 = {680, 225, 420, 320};
            SDL_SetRenderDrawColor(ren, menuCursor == 1 ? 254 : 20, menuCursor == 1 ? 240 : 83, menuCursor == 1 ? 138 : 45, 255);
            SDL_RenderFillRect(ren, &card1);
            drawText(ren, fontBig, "模式二：日本 13 张立直麻将", card1.x + 50, card1.y + 35, menuCursor == 1 ? cDark : cGold);
            drawText(ren, fontSmall, "・起手 13 张牌，支持特殊牌型(七对子)", card1.x + 30, card1.y + 100, menuCursor == 1 ? cDark : cWhite);
            drawText(ren, fontSmall, "・门清听牌按(ZR)拍棒立直 + 一发奖励", card1.x + 30, card1.y + 140, menuCursor == 1 ? cDark : cWhite);
            drawText(ren, fontSmall, "・含宝牌翻倍、满贯/跳满/役满结算", card1.x + 30, card1.y + 180, menuCursor == 1 ? cDark : cWhite);
            drawText(ren, fontSmall, "・初始点数：25,000 点", card1.x + 30, card1.y + 240, menuCursor == 1 ? cDark : cGold);

            SDL_RenderPresent(ren);
            continue;
        }

        if (kDown & HidNpadButton_Minus) { inMenu = true; continue; }
        if (kDown & HidNpadButton_R) startNewGame((kHeld & HidNpadButton_L) != 0);
        if (phase == 0 && !playerHand.empty()) {
            if (!playerRiichiDeclared) {
                if (kDown & HidNpadButton_Left) cursor = (cursor - 1 + (int)playerHand.size()) % (int)playerHand.size();
                if (kDown & HidNpadButton_Right) cursor = (cursor + 1) % (int)playerHand.size();
            } else {
                cursor = (int)playerHand.size() - 1;
            }

            if (isRiichiMode && !playerRiichiDeclared && (kDown & HidNpadButton_ZR)) {
                bool menQing = true;
                for (const auto& m : playerMelds) if (m.type != 3) menQing = false;
                auto tempHand = playerHand;
                tempHand.erase(tempHand.begin() + cursor);
                if (menQing && playerScore >= 1000 && !getTenpaiTiles(tempHand, true).empty()) {
                    playerRiichiDeclared = true;
                    playerIppatsu = true;
                    playerScore -= 1000;
                    playSfx(gSfxGang, gSfxPeng);
                    Tile out = playerHand.at(cursor);
                    playerHand.erase(playerHand.begin() + cursor);
                    discards.push_back(out);
                    std::sort(playerHand.begin(), playerHand.end());
                    turn = 0; phase = 1; aiTimer = SDL_GetTicks();
                    statusMsg = "(立直宣言！) 投入1000点立直棒，等待牌友出牌...";
                } else {
                    statusMsg = "无法立直(需门前清、点数>=1000且打出当前牌后听牌)";
                }
            } else if ((kDown & HidNpadButton_Y) && canWinShape(playerHand, isRiichiMode, true)) {
                playSfx(gSfxHu);
                applyWinResult(evaluateWinFull(3, "你", playerHand, playerMelds, isRiichiMode, true, justDidGang,
                                               playerRiichiDeclared, playerIppatsu, actualDora, -1, streakCount));
            } else if (!playerRiichiDeclared && (kDown & HidNpadButton_L)) {
                bool didGang = false;
                for (const auto& t : playerHand) {
                    if (countTile(playerHand, t) == 4) {
                        for (int k = 0; k < 4; ++k) removeOneTile(playerHand, t.suit, t.val);
                        playerMelds.push_back({3, {t, t, t, t}});
                        didGang = true; break;
                    }
                }
                if (!didGang) {
                    for (auto& m : playerMelds) {
                        Tile firstTile = m.tiles.at(0);
                        if (m.type == 1 && hasTileVal(playerHand, firstTile.suit, firstTile.val)) {
                            removeOneTile(playerHand, firstTile.suit, firstTile.val);
                            m.type = 2; m.tiles.push_back(firstTile);
                            didGang = true; break;
                        }
                    }
                }
                if (didGang) {
                    playSfx(gSfxGang, gSfxPlay);
                    playerDrawAndEnterTurn(true);
                    statusMsg = "开杠补牌成功！请选择一张打出";
                }
            } else if (kDown & HidNpadButton_A) {
                Tile out = playerHand.at(cursor);
                playerHand.erase(playerHand.begin() + cursor);
                discards.push_back(out);
                playSfx(gSfxPlay);
                justDidGang = false;
                if (playerRiichiDeclared) playerIppatsu = false;
                std::sort(playerHand.begin(), playerHand.end());
                if (cursor >= (int)playerHand.size()) cursor = std::max(0, (int)playerHand.size() - 1);

                bool aiStoleWin = false;
                for (int i = 0; i < 3; ++i) {
                    auto aiTest = ais.at(i).hand;
                    aiTest.push_back(out);
                    if (canWinShape(aiTest, isRiichiMode, true)) {
                        playSfx(gSfxHu);
                        ais.at(i).bubble = "哈哈！点炮啦，这张我胡了！";
                        applyWinResult(evaluateWinFull(i, ais.at(i).name, aiTest, ais.at(i).melds, isRiichiMode, false, false,
                                                       false, false, actualDora, 3, 0));
                        aiStoleWin = true; break;
                    }
                }
                if (!aiStoleWin) {
                    turn = 0; phase = 1; aiTimer = SDL_GetTicks();
                    statusMsg = "等待牌友出牌...";
                }
            }
        }
        else if (phase == 1) {
            if (SDL_GetTicks() - aiTimer >= 420) {
                if (wall.empty()) {
                    phase = 3; hasWinPanel = false;
                    statusMsg = "流局！牌堆已摸完，按(R)继续下一局";
                } else {
                    AIPlayer& ai = ais.at(turn);
                    ai.hand.push_back(wall.back()); wall.pop_back();
                    if (canWinShape(ai.hand, isRiichiMode, true)) {
                        playSfx(gSfxHu);
                        ai.bubble = "不好意思，我自摸胡牌啦！";
                        applyWinResult(evaluateWinFull(turn, ai.name, ai.hand, ai.melds, isRiichiMode, true, false,
                                                       false, false, actualDora, -1, 0));
                    } else {
                        int dropIdx = pickAIDiscard(ai.hand);
                        Tile out = ai.hand.at(dropIdx);
                        ai.hand.erase(ai.hand.begin() + dropIdx);
                        discards.push_back(out);
                        ai.bubble = ai.lines.at(std::rand() % ai.lines.size());
                        playSfx(gSfxTaunt, gSfxPlay);

                        int whoDiscarded = turn;
                        turn = (turn + 1) % 4;
                        if (!checkPlayerReactions(out, whoDiscarded)) {
                            if (turn == 3) playerDrawAndEnterTurn(false);
                            else aiTimer = SDL_GetTicks();
                        }
                    }
                }
            }
        } else if (phase == 2) {
            if ((kDown & HidNpadButton_Y) && optHu) {
                playerHand.push_back(lastDiscard);
                std::sort(playerHand.begin(), playerHand.end());
                playSfx(gSfxHu);
                applyWinResult(evaluateWinFull(3, "你", playerHand, playerMelds, isRiichiMode, false, false,
                                               playerRiichiDeclared, playerIppatsu, actualDora, lastDiscardFrom, streakCount));
            } else if ((kDown & HidNpadButton_L) && optGang) {
                discards.pop_back();
                for (int k = 0; k < 3; ++k) removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                playerMelds.push_back({2, {lastDiscard, lastDiscard, lastDiscard, lastDiscard}});
                playSfx(gSfxGang, gSfxPlay);
                playerDrawAndEnterTurn(true);
                statusMsg = "明杠成功并补牌！请打出一张牌";
            } else if ((kDown & HidNpadButton_X) && optPeng) {
                discards.pop_back();
                removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                playerMelds.push_back({1, {lastDiscard, lastDiscard, lastDiscard}});
                playSfx(gSfxPeng, gSfxPlay);
                std::sort(playerHand.begin(), playerHand.end());
                cursor = 0; turn = 3; phase = 0; justDidGang = false;
                statusMsg = "碰牌成功！请选择一张牌打出";
            } else if ((kDown & HidNpadButton_A) && !optChis.empty()) {
                discards.pop_back();
                auto pairV = optChis.at(0);
                removeOneTile(playerHand, lastDiscard.suit, pairV.first);
                removeOneTile(playerHand, lastDiscard.suit, pairV.second);
                std::vector<Tile> meldTiles = {
                    {lastDiscard.suit, pairV.first, makeTileLabel(lastDiscard.suit, pairV.first)},
                    lastDiscard,
                    {lastDiscard.suit, pairV.second, makeTileLabel(lastDiscard.suit, pairV.second)}
                };
                std::sort(meldTiles.begin(), meldTiles.end());
                playerMelds.push_back({0, meldTiles});
                playSfx(gSfxChi, gSfxPlay);
                std::sort(playerHand.begin(), playerHand.end());
                cursor = 0; turn = 3; phase = 0; justDidGang = false;
                statusMsg = "吃牌成功！请选择一张牌打出";
            } else if (kDown & HidNpadButton_B) {
                if (turn == 3) playerDrawAndEnterTurn(false);
                else { phase = 1; aiTimer = SDL_GetTicks(); statusMsg = "等待牌友出牌..."; }
            }
        }
        SDL_SetRenderDrawColor(ren, isRiichiMode ? 18 : 20, isRiichiMode ? 65 : 83, isRiichiMode ? 75 : 45, 255);
        SDL_RenderClear(ren);

        SDL_Color cWhite = {255, 255, 255, 255}, cYellow = {253, 224, 71, 255};
        SDL_Color cDark = {20, 20, 20, 255}, cCyan = {103, 232, 249, 255}, cGold = {251, 191, 36, 255};

        drawText(ren, fontSmall, statusMsg, 30, 14, cYellow);
        std::string topRight = "第" + std::to_string(roundNum) + "局 | 点数:" + std::to_string(playerScore) + " | 余牌:" + std::to_string(wall.size()) + " | (-)换模式";
        drawText(ren, fontSmall, topRight, 700, 14, cGold);

        std::vector<int> displayOrder = {2, 1, 0};
        for (int idx = 0; idx < 3; ++idx) {
            int aiIdx = displayOrder.at(idx);
            SDL_Rect box = {30 + idx * 415, 50, 390, 105};
            SDL_SetRenderDrawColor(ren, 12, 48, 30, 255);
            SDL_RenderFillRect(ren, &box);

            SDL_Rect avRect = {box.x + 10, box.y + 11, 82, 82};
            SDL_Texture* curTex = aiNormal.at(aiIdx);
            if (phase == 3 && hasWinPanel) {
                if (lastWinResult.scoreDeltas.at(aiIdx) > 0 && aiWin.at(aiIdx)) curTex = aiWin.at(aiIdx);
                else if (lastWinResult.scoreDeltas.at(aiIdx) < 0 && aiLose.at(aiIdx)) curTex = aiLose.at(aiIdx);
            }
            if (curTex) SDL_RenderCopy(ren, curTex, nullptr, &avRect);

            std::string header = ais.at(aiIdx).name + " (" + std::to_string(ais.at(aiIdx).score) + ")";
            drawText(ren, fontSmall, header, box.x + 102, box.y + 10, cWhite);

            SDL_Rect bub = {box.x + 102, box.y + 46, 276, 44};
            SDL_SetRenderDrawColor(ren, 254, 240, 138, 255);
            SDL_RenderFillRect(ren, &bub);
            drawText(ren, fontSmall, ais.at(aiIdx).bubble, bub.x + 10, bub.y + 10, cDark);
        }

        SDL_Rect river = {30, 172, 1220, 252};
        SDL_SetRenderDrawColor(ren, 14, 58, 36, 255);
        SDL_RenderFillRect(ren, &river);

        std::string riverTitle = isRiichiMode
            ? ("牌河 (宝牌指示: " + doraIndicator.label + " -> 宝牌: " + actualDora.label + ")")
            : ("牌河 (台麻16张 | 当前连庄: " + std::to_string(streakCount) + ")");
        drawText(ren, fontSmall, riverTitle, 45, 180, cWhite);

        int startIdx = std::max(0, (int)discards.size() - 28);
        for (int i = startIdx; i < (int)discards.size(); ++i) {
            int idx = i - startIdx;
            int col = idx % 14, row = idx / 14;
            SDL_Rect tRect = {45 + col * 85, 215 + row * 96, 76, 84};
            SDL_SetRenderDrawColor(ren, 245, 245, 220, 255);
            SDL_RenderFillRect(ren, &tRect);
            drawText(ren, fontBig, discards.at(i).label, tRect.x + 10, tRect.y + 24, getTileColor(discards.at(i)));
        }

        if (phase == 2) {
            SDL_Rect banner = {220, 255, 840, 90};
            SDL_SetRenderDrawColor(ren, 127, 29, 29, 240);
            SDL_RenderFillRect(ren, &banner);
            drawText(ren, fontBig, "打出(" + lastDiscard.label + ")！ " + statusMsg, banner.x + 30, banner.y + 28, cYellow);
        }

        if (phase == 3 && hasWinPanel) {
            SDL_Rect panel = {180, 180, 920, 245};
            SDL_SetRenderDrawColor(ren, lastWinResult.winnerSeat == 3 ? 120 : 40, 20, 20, 245);
            SDL_RenderFillRect(ren, &panel);
            drawText(ren, fontBig, lastWinResult.title, panel.x + 35, panel.y + 22, cGold);
            drawText(ren, fontSmall, "番型明细：" + lastWinResult.details, panel.x + 35, panel.y + 72, cWhite);

            std::string deltaLine = "本局变动：你 " + (lastWinResult.scoreDeltas.at(3) >= 0 ? std::string("+") : std::string("")) + std::to_string(lastWinResult.scoreDeltas.at(3));
            for (int i = 0; i < 3; ++i) {
                if (lastWinResult.scoreDeltas.at(i) != 0) {
                    std::string sgn = lastWinResult.scoreDeltas.at(i) >= 0 ? "+" : "";
                    deltaLine += " | " + ais.at(i).name + " " + sgn + std::to_string(lastWinResult.scoreDeltas.at(i));
                }
            }
            drawText(ren, fontSmall, deltaLine, panel.x + 35, panel.y + 122, cCyan);
            drawText(ren, fontSmall, "按(R)继续下一局 | 按住(L+R)重置筹码 | 按(-)切换台麻/日麻", panel.x + 35, panel.y + 182, cYellow);
        }

        std::string infoLine = "";
        if (playerRiichiDeclared) infoLine += "(已立直) ";
        if (phase == 0 && !playerRiichiDeclared) {
            bool canSelfGang = false;
            for (const auto& t : playerHand) if (countTile(playerHand, t) == 4) canSelfGang = true;
            for (const auto& m : playerMelds) {
                Tile firstTile = m.tiles.at(0);
                if (m.type == 1 && hasTileVal(playerHand, firstTile.suit, firstTile.val)) canSelfGang = true;
            }
            if (canSelfGang) infoLine += "(按L键开杠) ";
            if (isRiichiMode) {
                bool menQing = true;
                for (const auto& m : playerMelds) if (m.type != 3) menQing = false;
                auto tempHand = playerHand;
                if (!tempHand.empty()) tempHand.erase(tempHand.begin() + cursor);
                if (menQing && playerScore >= 1000 && !getTenpaiTiles(tempHand, true).empty()) {
                    infoLine += "(打出此牌听牌！可按ZR拍棒立直) ";
                }
            }
        } else if (phase == 1 || phase == 2) {
            auto waits = getTenpaiTiles(playerHand, isRiichiMode);
            if (!waits.empty()) {
                infoLine += "已听牌！胡：";
                for (size_t i = 0; i < waits.size(); ++i) infoLine += waits.at(i).label + (i + 1 < waits.size() ? "、" : "");
            }
        }
        if (!infoLine.empty()) drawText(ren, fontSmall, infoLine, 35, 438, cCyan);

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

    if (fLog) fclose(fLog);
    for (auto* t : aiNormal) if (t) SDL_DestroyTexture(t);
    for (auto* t : aiWin) if (t) SDL_DestroyTexture(t);
    for (auto* t : aiLose) if (t) SDL_DestroyTexture(t);
    Mix_FreeChunk(gSfxPlay); Mix_FreeChunk(gSfxChi); Mix_FreeChunk(gSfxPeng);
    Mix_FreeChunk(gSfxGang); Mix_FreeChunk(gSfxHu); Mix_FreeChunk(gSfxTaunt);
    for (auto* m : gBgmList) Mix_FreeMusic(m);
    if (fontTitle) TTF_CloseFont(fontTitle);
    if (fontBig) TTF_CloseFont(fontBig);
    if (fontSmall) TTF_CloseFont(fontSmall);
    Mix_CloseAudio();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    romfsExit();
    return 0;
}
