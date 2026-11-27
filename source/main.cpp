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
#include <cmath>
#include <functional>

struct Tile {
    int suit;
    int val;
    std::string label;
    bool operator==(const Tile& o) const { return suit == o.suit && val == o.val; }
    bool operator<(const Tile& o) const { return (suit != o.suit) ? (suit < o.suit) : (val < o.val); }
};

struct Meld {
    int type; // 0:吃 1:碰 2:明杠/加杠 3:暗杠
    std::vector<Tile> tiles;
};

struct AIPlayer {
    std::string name;
    std::string bubble;
    int score;
    std::vector<Tile> hand;
    std::vector<Meld> melds;
    std::vector<std::string> lines;
    std::string huLine;
    std::string loseLine;
    std::string chiLine;
    std::string pengLine;
    std::string gangLine;
    std::vector<Mix_Chunk*> lineVoices;
    Mix_Chunk* huVoice = nullptr;
    Mix_Chunk* loseVoice = nullptr;
    Mix_Chunk* chiVoice = nullptr;
    Mix_Chunk* pengVoice = nullptr;
    Mix_Chunk* gangVoice = nullptr;
    Mix_Chunk* tileVoices[4][10] = {};
};

struct WinResult {
    int winnerSeat;
    std::string title;
    std::string details;
    std::vector<int> scoreDeltas;
    std::vector<Tile> horseTiles;
    int hitHorseCount = 0;
};

std::vector<Mix_Music*> gBgmList;
int gCurrentBgm = -1;
Mix_Chunk* gSfxPlay = nullptr;
Mix_Chunk* gSfxChi = nullptr;
Mix_Chunk* gSfxPeng = nullptr;
Mix_Chunk* gSfxGang = nullptr;
Mix_Chunk* gSfxHu = nullptr;
Mix_Chunk* gSfxTaunt = nullptr;
Mix_Chunk* gPlayerChi = nullptr;
Mix_Chunk* gPlayerPeng = nullptr;
Mix_Chunk* gPlayerGang = nullptr;
Mix_Chunk* gPlayerHu = nullptr;

int gVolBgm = 45;
int gVolVoice = 100;
int gVolSfx = 80;

void loadVolumeConfig() {
    FILE* f = fopen("sdmc:/mahjong_cfg.txt", "r");
    if (f) {
        int b = 45, v = 100, s = 80;
        if (fscanf(f, "%d %d %d", &b, &v, &s) == 3) {
            gVolBgm = std::max(0, std::min(100, b));
            gVolVoice = std::max(0, std::min(100, v));
            gVolSfx = std::max(0, std::min(100, s));
        }
        fclose(f);
    }
}

void saveVolumeConfig() {
    FILE* f = fopen("sdmc:/mahjong_cfg.txt", "w");
    if (f) {
        fprintf(f, "%d %d %d\n", gVolBgm, gVolVoice, gVolSfx);
        fclose(f);
    }
}

void applyBgmVolume() {
    Mix_VolumeMusic((gVolBgm * MIX_MAX_VOLUME) / 100);
}

void playRandomBgm() {
    if (gBgmList.empty()) return;
    int nextIdx = std::rand() % (int)gBgmList.size();
    if (gBgmList.size() > 1 && nextIdx == gCurrentBgm) nextIdx = (nextIdx + 1) % (int)gBgmList.size();
    gCurrentBgm = nextIdx;
    applyBgmVolume();
    Mix_PlayMusic(gBgmList.at(gCurrentBgm), 1);
}

void playSfx(Mix_Chunk* sfx, Mix_Chunk* fallback = nullptr) {
    Mix_Chunk* target = sfx ? sfx : fallback;
    if (target) {
        Mix_VolumeChunk(target, (gVolSfx * MIX_MAX_VOLUME) / 100);
        Mix_PlayChannel(-1, target, 0);
    }
}

void playVoice(Mix_Chunk* voice, Mix_Chunk* fallback = nullptr, bool interrupt = true) {
    Mix_Chunk* target = voice ? voice : fallback;
    if (target) {
        if (!interrupt && Mix_Playing(0)) return;
        Mix_HaltChannel(0);
        Mix_VolumeChunk(target, (gVolVoice * MIX_MAX_VOLUME) / 100);
        Mix_PlayChannel(0, target, 0);
    }
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

std::vector<Tile> buildWall(bool noHonors = false) {
    std::vector<Tile> deck;
    for (int s = 0; s < 3; ++s) {
        for (int v = 1; v <= 9; ++v) {
            for (int c = 0; c < 4; ++c) deck.push_back({s, v, makeTileLabel(s, v)});
        }
    }
    if (!noHonors) {
        for (int v = 1; v <= 7; ++v) {
            for (int c = 0; c < 4; ++c) deck.push_back({3, v, makeTileLabel(3, v)});
        }
    }
    u64 hwRand = 0;
    randomGet(&hwRand, sizeof(hwRand));
    static std::mt19937_64 g(svcGetSystemTick() ^ hwRand ^ (u64)std::time(nullptr));
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
                          int gameMode, bool isSelfDraw, bool isGangBloom, bool declaredRiichi, bool isIppatsu,
                          const Tile& doraTile, int loserSeat, int streakCount, std::vector<Tile>& wall) {
    // gameMode: 0=台湾16张, 1=日本立直, 2=四川血战(第二步完善), 3=广东推倒胡
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
    bool isChiitoi = ((gameMode == 1 || gameMode == 3) && melds.empty() && isSevenPairs(hand));

    if (gameMode == 0) {
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
    } else if (gameMode == 1) {
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
    } else if (gameMode == 2) {
        // 模式三：四川血战到底正宗番型算分（底分 500，番数翻倍）
        int fan = 0;
        std::string desc = "平胡(1倍)";
        if (isSelfDraw) { fan += 1; desc += " + 自摸(1番)"; }
        if (isGangBloom) { fan += 1; desc += " + 杠上开花(1番)"; }
        if (isChiitoi) { fan += 2; desc += " + 暗七对(2番)"; }
        else if (isToitoi) { fan += 1; desc += " + 对对胡(1番)"; }
        if (isFlush) { fan += 2; desc += " + 清一色(2番)"; }

        // 计算“根”（手牌或明牌里有 4 张一样的牌，每根 +1 番）
        std::map<int, int> allCnt;
        for (const auto& t : hand) allCnt[t.suit * 100 + t.val]++;
        for (const auto& m : melds) for (const auto& t : m.tiles) allCnt[t.suit * 100 + t.val]++;
        int roots = 0;
        for (const auto& kv : allCnt) if (kv.second == 4) roots++;
        if (roots > 0) {
            fan += roots;
            desc += " + 带" + std::to_string(roots) + "根(+" + std::to_string(roots) + "番)";
        }

        int mult = 1 << std::min(fan, 5); // 最高 32 倍封顶
        int basePay = 500 * mult;
        if (isSelfDraw) {
            res.title = "(四川血战) " + winnerName + " 自摸胡牌！共 " + std::to_string(mult) + " 倍";
            for (int i = 0; i < 4; ++i) if (i != winnerSeat) res.scoreDeltas.at(i) = -basePay;
            res.scoreDeltas.at(winnerSeat) = basePay * 3;
        } else {
            res.title = "(四川血战) " + winnerName + " 点炮胡牌！共 " + std::to_string(mult) + " 倍";
            res.scoreDeltas.at(loserSeat) = -basePay;
            res.scoreDeltas.at(winnerSeat) = basePay;
        }
        res.details = desc;
    } else {
        // 模式四：广东推倒胡 + 4张买马开奖
        int mult = 1;
        std::string desc = "推倒平胡(1倍)";
        if (isSelfDraw) { mult += 1; desc += " + 自摸(+1倍)"; }
        if (isGangBloom) { mult += 2; desc += " + 杠上开花(+2倍)"; }
        if (isChiitoi) { mult += 2; desc += " + 七对子(+2倍)"; }
        else if (isToitoi) { mult += 1; desc += " + 碰碰胡(+1倍)"; }
        if (isFlush) { mult += 3; desc += " + 清一色(+3倍)"; }
        else if (isHalfFlush) { mult += 1; desc += " + 混一色(+1倍)"; }

        for (int k = 0; k < 4 && !wall.empty(); ++k) {
            Tile ht = wall.back();
            wall.pop_back();
            res.horseTiles.push_back(ht);
            bool isHit = (ht.suit < 3 && (ht.val == 1 || ht.val == 5 || ht.val == 9)) || (ht.suit == 3 && ht.val == 1);
            if (isHit) res.hitHorseCount++;
        }
        if (res.hitHorseCount > 0) {
            mult += res.hitHorseCount;
            desc += " + 中" + std::to_string(res.hitHorseCount) + "马(+" + std::to_string(res.hitHorseCount) + "倍)";
        } else {
            desc += " + 买马未中";
        }

        int baseScore = 600 * mult;
        if (isSelfDraw) {
            res.title = "(广东推倒胡) " + winnerName + " 自摸！总计 " + std::to_string(mult) + " 倍";
            for (int i = 0; i < 4; ++i) if (i != winnerSeat) res.scoreDeltas.at(i) = -baseScore;
            res.scoreDeltas.at(winnerSeat) = baseScore * 3;
        } else {
            int pay = baseScore * 2;
            res.title = "(广东推倒胡) " + winnerName + " 抢胡(点炮)！总计 " + std::to_string(mult) + " 倍";
            res.scoreDeltas.at(loserSeat) = -pay;
            res.scoreDeltas.at(winnerSeat) = pay;
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

int pickAIDiscard(const std::vector<Tile>& hand, int voidSuit = -1) {
    if (voidSuit >= 0) {
        for (int i = 0; i < (int)hand.size(); ++i) {
            if (hand.at(i).suit == voidSuit) return i;
        }
    }
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

struct CachedText {
    SDL_Texture* tex;
    int w;
    int h;
};
std::map<std::string, CachedText> gTextCache;

void drawTextImpl(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, int maxW, SDL_Color color) {
    if (!font || text.empty()) return;
    std::string key = std::to_string((uintptr_t)font) + "|" + std::to_string(maxW) + "|" +
                      std::to_string(color.r) + "_" + std::to_string(color.g) + "_" + std::to_string(color.b) + "|" + text;
    auto it = gTextCache.find(key);
    if (it == gTextCache.end()) {
        if (gTextCache.size() > 600) {
            for (auto& kv : gTextCache) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
            gTextCache.clear();
        }
        SDL_Surface* surf = (maxW > 0)
            ? TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, (Uint32)maxW)
            : TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
        CachedText ct{tex, surf->w, surf->h};
        SDL_FreeSurface(surf);
        if (!tex) return;
        it = gTextCache.emplace(key, ct).first;
    }
    SDL_Rect dst = {x, y, it->second.w, it->second.h};
    SDL_RenderCopy(ren, it->second.tex, nullptr, &dst);
}

void drawText(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    drawTextImpl(ren, font, text, x, y, 0, color);
}

void drawTextWrapped(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, int maxW, SDL_Color color) {
    drawTextImpl(ren, font, text, x, y, maxW, color);
}

void drawCircle(SDL_Renderer* ren, int cx, int cy, int r, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) SDL_RenderDrawPoint(ren, cx + dx, cy + dy);
        }
    }
}

void drawPip(SDL_Renderer* ren, int cx, int cy, int r, SDL_Color outerCol) {
    drawCircle(ren, cx, cy, r, outerCol);
    drawCircle(ren, cx, cy, std::max(2, r - 3), {250, 250, 245, 255});
    drawCircle(ren, cx, cy, std::max(1, r / 2), outerCol);
}

void drawBamboo(SDL_Renderer* ren, int x, int y, int w, int h, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
    SDL_Rect r1 = {x, y, w, h / 2 - 1};
    SDL_Rect r2 = {x, y + h / 2 + 1, w, h / 2 - 1};
    SDL_RenderFillRect(ren, &r1);
    SDL_RenderFillRect(ren, &r2);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
    SDL_RenderDrawLine(ren, x + w / 2, y + 2, x + w / 2, y + h - 2);
}
void drawMahjongTile(SDL_Renderer* ren, TTF_Font* fontBig, TTF_Font* fontSmall, const Tile& t, SDL_Rect rect, bool selected) {
    SDL_Rect base = {rect.x, rect.y + 4, rect.w, rect.h};
    SDL_SetRenderDrawColor(ren, 16, 110, 65, 255);
    SDL_RenderFillRect(ren, &base);

    SDL_SetRenderDrawColor(ren, selected ? 255 : 252, selected ? 252 : 250, selected ? 210 : 242, 255);
    SDL_RenderFillRect(ren, &rect);
    SDL_SetRenderDrawColor(ren, selected ? 234 : 180, selected ? 179 : 175, selected ? 8 : 160, 255);
    SDL_RenderDrawRect(ren, &rect);

    SDL_Color cRed = {205, 30, 30, 255};
    SDL_Color cBlue = {25, 80, 195, 255};
    SDL_Color cGreen = {20, 135, 55, 255};
    SDL_Color cBlack = {25, 30, 40, 255};

    int cx = rect.x + rect.w / 2;
    int cy = rect.y + rect.h / 2;

    if (t.suit == 0) {
        std::vector<std::string> nums = {"一", "二", "三", "四", "五", "六", "七", "八", "九"};
        if (rect.w < 38) {
            drawText(ren, fontSmall, nums.at(t.val - 1), rect.x + (rect.w - 16) / 2, rect.y + 1, cBlack);
            drawText(ren, fontSmall, "萬", rect.x + (rect.w - 16) / 2, rect.y + rect.h / 2 - 1, cRed);
        } else {
            int topY = rect.y + rect.h * 12 / 100;
            int botY = rect.y + rect.h * 52 / 100;
            drawText(ren, fontSmall, nums.at(t.val - 1), cx - 9, topY, cBlack);
            drawText(ren, fontBig, "萬", cx - 12, botY, cRed);
        }
    } else if (t.suit == 1) {
        int r = std::max(5, rect.w / 7);
        int xL = rect.x + rect.w * 3 / 10, xR = rect.x + rect.w * 7 / 10;
        int yT = rect.y + rect.h * 22 / 100, yM = cy, yB = rect.y + rect.h * 78 / 100;

        if (t.val == 1) {
            drawPip(ren, cx, cy, rect.w / 3, cRed);
            drawCircle(ren, cx, cy, rect.w / 6, cBlue);
        } else if (t.val == 2) {
            drawPip(ren, cx, rect.y + rect.h * 3 / 10, r + 2, cGreen);
            drawPip(ren, cx, rect.y + rect.h * 7 / 10, r + 2, cBlue);
        } else if (t.val == 3) {
            drawPip(ren, xL - 2, yT, r, cBlue);
            drawPip(ren, cx, cy, r, cRed);
            drawPip(ren, xR + 2, yB, r, cGreen);
        } else if (t.val == 4) {
            drawPip(ren, xL, yT + 4, r + 1, cBlue); drawPip(ren, xR, yT + 4, r + 1, cGreen);
            drawPip(ren, xL, yB - 4, r + 1, cGreen); drawPip(ren, xR, yB - 4, r + 1, cBlue);
        } else if (t.val == 5) {
            drawPip(ren, xL, yT + 2, r, cBlue); drawPip(ren, xR, yT + 2, r, cGreen);
            drawPip(ren, cx, cy, r, cRed);
            drawPip(ren, xL, yB - 2, r, cGreen); drawPip(ren, xR, yB - 2, r, cBlue);
        } else if (t.val == 6) {
            drawPip(ren, xL, yT, r, cGreen); drawPip(ren, xR, yT, r, cGreen);
            drawPip(ren, xL, yM + 6, r, cRed); drawPip(ren, xR, yM + 6, r, cRed);
            drawPip(ren, xL, yB + 2, r, cRed); drawPip(ren, xR, yB + 2, r, cRed);
        } else if (t.val == 7) {
            drawPip(ren, xL - 3, yT - 4, r - 1, cGreen);
            drawPip(ren, cx, yT + 5, r - 1, cGreen);
            drawPip(ren, xR + 3, yT + 14, r - 1, cGreen);
            drawPip(ren, xL, yM + 10, r - 1, cRed); drawPip(ren, xR, yM + 10, r - 1, cRed);
            drawPip(ren, xL, yB + 3, r - 1, cRed); drawPip(ren, xR, yB + 3, r - 1, cRed);
        } else if (t.val == 8) {
            for (int row = 0; row < 4; ++row) {
                int py = rect.y + rect.h * (row + 1) / 5;
                drawPip(ren, xL, py, r - 1, cBlue);
                drawPip(ren, xR, py, r - 1, cBlue);
            }
        } else if (t.val == 9) {
            for (int col = 0; col < 3; ++col) {
                int px = rect.x + rect.w * (col + 1) / 4;
                drawPip(ren, px, yT, r - 1, cBlue);
                drawPip(ren, px, yM, r - 1, cRed);
                drawPip(ren, px, yB, r - 1, cGreen);
            }
        }
    } else if (t.suit == 2) {
        int bw = std::max(4, rect.w / 9);
        int xL = rect.x + rect.w * 3 / 10 - bw / 2, xR = rect.x + rect.w * 7 / 10 - bw / 2, xM = cx - bw / 2;
        int bh2 = (rect.h - 24) / 2, bh3 = (rect.h - 28) / 3;

        if (t.val == 1) {
            drawCircle(ren, cx, cy + 4, rect.w / 4, cGreen);
            drawCircle(ren, cx - 3, cy - 8, rect.w / 6, cRed);
            drawBamboo(ren, cx - 2, cy + 8, 5, rect.h / 4, cGreen);
        } else if (t.val == 2) {
            drawBamboo(ren, xM, rect.y + 10, bw + 1, bh2, cBlue);
            drawBamboo(ren, xM, rect.y + 14 + bh2, bw + 1, bh2, cGreen);
        } else if (t.val == 3) {
            drawBamboo(ren, xM, rect.y + 10, bw + 1, bh2, cBlue);
            drawBamboo(ren, xL, rect.y + 14 + bh2, bw + 1, bh2, cGreen);
            drawBamboo(ren, xR, rect.y + 14 + bh2, bw + 1, bh2, cGreen);
        } else if (t.val == 4) {
            drawBamboo(ren, xL, rect.y + 10, bw + 1, bh2, cBlue);
            drawBamboo(ren, xR, rect.y + 10, bw + 1, bh2, cGreen);
            drawBamboo(ren, xL, rect.y + 14 + bh2, bw + 1, bh2, cGreen);
            drawBamboo(ren, xR, rect.y + 14 + bh2, bw + 1, bh2, cBlue);
        } else if (t.val == 5) {
            drawBamboo(ren, xL, rect.y + 10, bw, bh2, cGreen);
            drawBamboo(ren, xR, rect.y + 10, bw, bh2, cBlue);
            drawBamboo(ren, xM, cy - bh2 / 2, bw, bh2, cRed);
            drawBamboo(ren, xL, rect.y + 14 + bh2, bw, bh2, cBlue);
            drawBamboo(ren, xR, rect.y + 14 + bh2, bw, bh2, cGreen);
        } else if (t.val == 6) {
            for (int col = 0; col < 3; ++col) {
                int bx = rect.x + rect.w * (col + 1) / 4 - bw / 2;
                drawBamboo(ren, bx, rect.y + 10, bw, bh2, cGreen);
                drawBamboo(ren, bx, rect.y + 14 + bh2, bw, bh2, cGreen);
            }
        } else if (t.val == 7) {
            drawBamboo(ren, xM, rect.y + 8, bw, bh3, cRed);
            for (int col = 0; col < 3; ++col) {
                int bx = rect.x + rect.w * (col + 1) / 4 - bw / 2;
                drawBamboo(ren, bx, rect.y + 12 + bh3, bw, bh3, cGreen);
                drawBamboo(ren, bx, rect.y + 16 + bh3 * 2, bw, bh3, cGreen);
            }
        } else if (t.val == 8) {
            int topY1 = rect.y + rect.h * 14 / 100;
            int topY2 = rect.y + rect.h * 44 / 100;
            int botY1 = rect.y + rect.h * 56 / 100;
            int botY2 = rect.y + rect.h * 86 / 100;
            int col1 = rect.x + rect.w * 24 / 100 - bw / 2;
            int col4 = rect.x + rect.w * 76 / 100 - bw / 2;
            int midL = rect.x + rect.w * 34 / 100;
            int midR = rect.x + rect.w * 66 / 100;
            int hHalf = topY2 - topY1;

            drawBamboo(ren, col1, topY1, bw, hHalf, cGreen);
            drawBamboo(ren, col4, topY1, bw, hHalf, cGreen);
            drawBamboo(ren, col1, botY1, bw, hHalf, cGreen);
            drawBamboo(ren, col4, botY1, bw, hHalf, cGreen);

            auto drawSlant = [&](int x1, int y1, int x2, int y2, SDL_Color col) {
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                for (int dx = -bw / 2; dx <= bw / 2; ++dx) {
                    SDL_RenderDrawLine(ren, x1 + dx, y1, x2 + dx, y2);
                }
            };
            drawSlant(cx - 1, topY1 + 2, midL, topY2 - 2, cRed);
            drawSlant(cx + 1, topY1 + 2, midR, topY2 - 2, cGreen);
            drawSlant(midL, botY1 + 2, cx - 1, botY2 - 2, cGreen);
            drawSlant(midR, botY1 + 2, cx + 1, botY2 - 2, cRed);
        } else if (t.val == 9) {
            for (int row = 0; row < 3; ++row) {
                int by = rect.y + 8 + row * (bh3 + 4);
                drawBamboo(ren, rect.x + rect.w / 4 - bw / 2, by, bw, bh3, cBlue);
                drawBamboo(ren, xM, by, bw, bh3, cRed);
                drawBamboo(ren, rect.x + rect.w * 3 / 4 - bw / 2, by, bw, bh3, cGreen);
            }
        }
    } else {
        if (t.val == 7) {
            SDL_Rect wb = {rect.x + 10, rect.y + 12, rect.w - 20, rect.h - 24};
            SDL_SetRenderDrawColor(ren, cBlue.r, cBlue.g, cBlue.b, 255);
            SDL_RenderDrawRect(ren, &wb);
            SDL_Rect wb2 = {wb.x + 3, wb.y + 3, wb.w - 6, wb.h - 6};
            SDL_RenderDrawRect(ren, &wb2);
        } else {
            std::vector<std::string> zNames = {"東", "南", "西", "北", "中", "發"};
            SDL_Color zCol = (t.val == 5) ? cRed : ((t.val == 6) ? cGreen : cBlack);
            drawText(ren, fontBig, zNames.at(t.val - 1), cx - 12, cy - 14, zCol);
        }
    }
}

void drawTileBack3D(SDL_Renderer* ren, SDL_Rect r, int side) {
    if (side == 0) {
        SDL_Rect whiteRim = {r.x, r.y, r.w, 6};
        SDL_SetRenderDrawColor(ren, 245, 245, 235, 255);
        SDL_RenderFillRect(ren, &whiteRim);

        SDL_Rect greenBack = {r.x, r.y + 5, r.w, r.h - 5};
        SDL_SetRenderDrawColor(ren, 22, 145, 78, 255);
        SDL_RenderFillRect(ren, &greenBack);

        SDL_SetRenderDrawColor(ren, 12, 85, 45, 255);
        SDL_RenderDrawRect(ren, &r);
    } else {
        int rimW = 6;
        SDL_Rect whiteRim = (side == 1) ? SDL_Rect{r.x + r.w - rimW, r.y, rimW, r.h} : SDL_Rect{r.x, r.y, rimW, r.h};
        SDL_Rect greenBack = (side == 1) ? SDL_Rect{r.x, r.y, r.w - rimW + 1, r.h} : SDL_Rect{r.x + rimW - 1, r.y, r.w - rimW + 1, r.h};

        SDL_SetRenderDrawColor(ren, 245, 245, 235, 255);
        SDL_RenderFillRect(ren, &whiteRim);
        SDL_SetRenderDrawColor(ren, 22, 145, 78, 255);
        SDL_RenderFillRect(ren, &greenBack);

        SDL_SetRenderDrawColor(ren, 12, 85, 45, 255);
        SDL_RenderDrawRect(ren, &r);
    }
}
int main(int argc, char* argv[]) {
    FILE* fLog = fopen("sdmc:/mahjong_log.txt", "w");
    auto logStep = [&](const char* msg) {
        if (fLog) { fprintf(fLog, "%s\n", msg); fflush(fLog); }
    };
    logStep("1: start main");

    loadVolumeConfig();

    std::srand((unsigned int)(svcGetSystemTick() ^ std::time(nullptr)));
    romfsInit();
    logStep("2: romfs ok");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    HidTouchScreenState prevTouch = {0};

    struct Confetti {
        float x, y, vx, vy;
        int w, h;
        SDL_Color col;
    };
    std::vector<Confetti> gParticles;
    Uint32 gWinFlashTimer = 0;

    auto spawnWinParticles = [&]() {
        gParticles.clear();
        gWinFlashTimer = SDL_GetTicks();
        std::vector<SDL_Color> cols = {
            {253, 224, 71, 255}, {248, 113, 113, 255}, {96, 165, 250, 255},
            {74, 222, 128, 255}, {251, 146, 60, 255}, {244, 114, 182, 255}
        };
        for (int i = 0; i < 65; ++i) {
            float angle = -3.14159f * (0.15f + 0.70f * (std::rand() % 100) / 100.0f);
            float speed = 4.0f + (std::rand() % 90) / 10.0f;
            gParticles.push_back({
                640.0f + (std::rand() % 240 - 120),
                320.0f + (std::rand() % 60 - 30),
                std::cos(angle) * speed,
                std::sin(angle) * speed - 3.0f,
                8 + std::rand() % 8,
                6 + std::rand() % 8,
                cols.at(i % cols.size())
            });
        }
    };
    logStep("3: pad ok");

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    logStep("4: sdl libs ok");

    SDL_Window* win = SDL_CreateWindow("Mahjong", 0, 0, 1280, 720, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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
    logStep("7: base audio ok");

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
    int gameMode = 0; // 0:台湾16张 1:日本立直 2:四川血战(第二步) 3:广东推倒胡
    bool showSettings = false;
    int settingsCursor = 0;

    std::vector<Tile> wall;
    std::vector<Tile> playerHand;
    std::vector<Meld> playerMelds;
    std::vector<Tile> discards;
    Tile doraIndicator{0, 1, "1万"};
    Tile actualDora{0, 2, "2万"};

    int playerScore = 50000, roundNum = 1, streakCount = 0;
    int totalWins = 0, totalLosses = 0;
    bool matchOverByBankrupt = false;

    std::vector<AIPlayer> ais = {
        {
            "下家・甜美辣妹", "人家今天手气超好的啦", 10000, {}, {},
            {"哎呀，手滑打错了", "这张人家不要哦", "听牌啦听牌啦！"},
            "哈哈！我胡牌啦，给钱给钱！",
            "呜呜，人家点炮输惨了...",
            "吃！顺手牵羊啦~",
            "碰！人家也要凑一对~",
            "开杠！再补一张牌~"
        },
        {
            "对家・淡定军师", "不急，慢慢算牌。", 10000, {}, {},
            {"嗯，稳扎稳打。", "让我想想...", "这把牌面不错。"},
            "承让了，这局我胡牌。",
            "失算了，竟然被你胡了...",
            "妙计，正好成顺。",
            "承让，这张我碰了。",
            "开杠，局势已定。"
        },
        {
            "上家・暴躁大哥", "准备好被我横扫了吗？", 10000, {}, {},
            {"这你都不信！", "快点啦，磨磨蹭蹭的！", "乱打小心点炮哦！"},
            "哈哈哈！老子胡牌了，拿钱来！",
            "可恶！这把居然输给你了！",
            "吃！算你小子识相！",
            "碰！都别跟老子抢！",
            "杠！准备接招吧！"
        }
    };

    for (int i = 0; i < 3; ++i) {
        std::string prefix = "romfs:/voices/ai" + std::to_string(i) + "_";
        ais[i].lineVoices = {
            Mix_LoadWAV((prefix + "1.wav").c_str()),
            Mix_LoadWAV((prefix + "2.wav").c_str()),
            Mix_LoadWAV((prefix + "3.wav").c_str())
        };
        ais[i].huVoice = Mix_LoadWAV((prefix + "hu.wav").c_str());
        ais[i].loseVoice = Mix_LoadWAV((prefix + "lose.wav").c_str());
        ais[i].chiVoice = Mix_LoadWAV((prefix + "chi.wav").c_str());
        ais[i].pengVoice = Mix_LoadWAV((prefix + "peng.wav").c_str());
        ais[i].gangVoice = Mix_LoadWAV((prefix + "gang.wav").c_str());
        for (int s = 0; s < 4; ++s) {
            int maxV = (s < 3) ? 9 : 7;
            for (int v = 1; v <= maxV; ++v) {
                std::string sv = std::to_string(s) + "_" + std::to_string(v) + ".wav";
                Mix_Chunk* tv = Mix_LoadWAV((prefix + "tile_" + sv).c_str());
                if (!tv) tv = Mix_LoadWAV(("romfs:/voices/tile_" + sv).c_str());
                ais[i].tileVoices[s][v] = tv;
            }
        }
    }
    logStep("8.5: character voices ok");

    auto drawSettingsOverlay = [&]() {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 185);
        SDL_Rect full = {0, 0, 1280, 720};
        SDL_RenderFillRect(ren, &full);

        SDL_Rect box = {320, 145, 640, 430};
        SDL_SetRenderDrawColor(ren, 16, 52, 34, 250);
        SDL_RenderFillRect(ren, &box);
        SDL_SetRenderDrawColor(ren, 251, 191, 36, 255);
        SDL_RenderDrawRect(ren, &box);

        SDL_Color cGold = {251, 191, 36, 255}, cWhite = {255, 255, 255, 255};
        SDL_Color cCyan = {103, 232, 249, 255}, cDark = {20, 20, 20, 255};

        drawText(ren, fontTitle, "音量与游戏设置", box.x + 195, box.y + 22, cGold);
        drawText(ren, fontSmall, "上下选项目 | 左右调音量(自动保存) | 按(+)或(B)返回", box.x + 85, box.y + 72, cCyan);

        std::vector<std::string> labels = {"背景音乐 (BGM)", "角色语音 (AI配音)", "打牌音效 (吃碰杠)", "保存并退出游戏"};
        std::vector<int> vals = {gVolBgm, gVolVoice, gVolSfx, -1};

        for (int i = 0; i < 4; ++i) {
            int rowY = box.y + 125 + i * 68;
            bool sel = (settingsCursor == i);
            SDL_Rect rowRect = {box.x + 40, rowY, 560, 52};
            SDL_SetRenderDrawColor(ren, sel ? 254 : 24, sel ? 240 : 75, sel ? 138 : 48, 255);
            SDL_RenderFillRect(ren, &rowRect);

            drawText(ren, fontBig, labels[i], rowRect.x + 20, rowRect.y + 12, sel ? cDark : cWhite);
            if (vals[i] >= 0) {
                SDL_Rect barBg = {rowRect.x + 265, rowRect.y + 16, 180, 20};
                SDL_SetRenderDrawColor(ren, 10, 30, 20, 255);
                SDL_RenderFillRect(ren, &barBg);

                SDL_Rect barFg = {barBg.x + 2, barBg.y + 2, (176 * vals[i]) / 100, 16};
                SDL_SetRenderDrawColor(ren, sel ? 22 : 251, sel ? 163 : 191, sel ? 74 : 36, 255);
                SDL_RenderFillRect(ren, &barFg);

                drawText(ren, fontBig, std::to_string(vals[i]) + "%", barBg.x + 192, rowRect.y + 12, sel ? cDark : cGold);
            } else {
                drawText(ren, fontSmall, "按 (A) 确认退出", rowRect.x + 360, rowRect.y + 15, sel ? cDark : cCyan);
            }
        }
    };
    int cursor = 0, turn = 3, phase = 0; // phase: 0=玩家出牌 1=AI思考 2=玩家抢牌 3=本局总结算 4=四川开局定缺选择
    Tile lastDiscard{0, 1, ""};
    int lastDiscardFrom = -1;
    bool justDidGang = false, playerRiichiDeclared = false, playerIppatsu = false, hasWinPanel = false;
    WinResult lastWinResult{3, "本局流局结算", "牌墙已摸完", {0, 0, 0, 0}, {}, 0};
    std::vector<std::string> xuezhanLogs; // 记录血战模式本局多次胡牌与刮风下雨明细
    std::vector<int> voidSuit = {-1, -1, -1, -1};     // 0=缺万 1=缺筒 2=缺条 -1=无
    std::vector<bool> hasWonSeat = {false, false, false, false}; // 血战模式：该座位是否已经胡牌
    int dingQueCursor = 0;

    bool optHu = false, optGang = false, optPeng = false;
    std::vector<std::pair<int, int>> optChis;
    std::string statusMsg = "";
    Uint32 aiTimer = 0;

    auto suitNameCN = [&](int s) -> std::string {
        if (s == 0) return "万";
        if (s == 1) return "筒";
        if (s == 2) return "条";
        return "";
    };

    auto hasVoidTiles = [&](const std::vector<Tile>& h, int vSuit) -> bool {
        if (vSuit < 0) return false;
        for (const auto& t : h) if (t.suit == vSuit) return true;
        return false;
    };

    auto activeUnwonCount = [&]() -> int {
        int c = 0;
        for (int i = 0; i < 4; ++i) if (!hasWonSeat.at(i)) c++;
        return c;
    };

    auto nextActiveSeat = [&](int cur) -> int {
        for (int step = 1; step <= 4; ++step) {
            int s = (cur + step) % 4;
            if (!hasWonSeat.at(s)) return s;
        }
        return (cur + 1) % 4;
    };

    // 四川血战：刮风下雨（明杠/暗杠立刻向未胡牌玩家收取杠钱）
    auto applySichuanGangMoney = [&](int seat, bool isAnGang) {
        if (gameMode != 2) return;
        int unit = isAnGang ? 600 : 300;
        int gain = 0;
        for (int i = 0; i < 4; ++i) {
            if (i != seat && !hasWonSeat.at(i)) {
                if (i == 3) playerScore -= unit;
                else ais.at(i).score -= unit;
                gain += unit;
            }
        }
        if (seat == 3) playerScore += gain;
        else ais.at(seat).score += gain;
        std::string who = (seat == 3) ? "你" : ais.at(seat).name;
        xuezhanLogs.push_back((isAnGang ? "【下雨】" : "【刮风】") + who + " 开杠收税 +" + std::to_string(gain));
    };

    auto removeOneTile = [&](std::vector<Tile>& h, int suit, int val) {
        for (auto it = h.begin(); it != h.end(); ++it) {
            if (it->suit == suit && it->val == val) { h.erase(it); return; }
        }
    };

    // 统一胡牌处理：普通模式直接结束，四川血战模式允许多人连续胡牌直到剩下 1 家未胡！
    auto handleSeatWin = [&](WinResult res, bool isSelfDraw, int loserSeat) {
        spawnWinParticles();
        if (gameMode != 2) {
            lastWinResult = res;
            for (int i = 0; i < 3; ++i) ais.at(i).score += res.scoreDeltas.at(i);
            playerScore += res.scoreDeltas.at(3);
            streakCount = (res.winnerSeat == 3) ? (streakCount + 1) : 0;
            if (res.scoreDeltas.at(3) > 0) totalWins++;
            else if (res.scoreDeltas.at(3) < 0) totalLosses++;

            if (playerScore < 0 || ais.at(0).score < 0 || ais.at(1).score < 0 || ais.at(2).score < 0) {
                matchOverByBankrupt = true;
                lastWinResult.title = "【破产打飞・比赛结束】" + lastWinResult.title;
                statusMsg = "有人负分破产，比赛结束！按(R)重置50000分新开比赛 | (-)回大厅";
            } else {
                statusMsg = "本局结束！按(R)下一局 | (L+R)重置筹码 | (-)切换玩法";
            }
            hasWinPanel = true;
            phase = 3;
            return;
        }

        // 四川血战模式：只向尚未胡牌的输家收分
        int w = res.winnerSeat;
        int totalWin = 0;
        if (isSelfDraw) {
            int eachPay = std::abs(res.scoreDeltas.at((w + 1) % 4));
            for (int i = 0; i < 4; ++i) {
                if (i != w && !hasWonSeat.at(i)) {
                    if (i == 3) playerScore -= eachPay;
                    else ais.at(i).score -= eachPay;
                    totalWin += eachPay;
                }
            }
        } else if (loserSeat >= 0) {
            int pay = std::abs(res.scoreDeltas.at(loserSeat));
            if (loserSeat == 3) playerScore -= pay;
            else ais.at(loserSeat).score -= pay;
            totalWin = pay;
        }
        if (w == 3) playerScore += totalWin;
        else ais.at(w).score += totalWin;

        hasWonSeat.at(w) = true;
        xuezhanLogs.push_back(res.title + " (+" + std::to_string(totalWin) + ") [" + res.details + "]");

        // 如果只剩 <= 1 家未胡牌，血战彻底结束！
        if (activeUnwonCount() <= 1 || wall.empty()) {
            lastWinResult = res;
            lastWinResult.title = "四川血战到底・本局总结算 (共 " + std::to_string(xuezhanLogs.size()) + " 次胡/杠)";
            lastWinResult.details = xuezhanLogs.back();
            if (hasWonSeat.at(3)) totalWins++;
            else totalLosses++;

            if (playerScore < 0 || ais.at(0).score < 0 || ais.at(1).score < 0 || ais.at(2).score < 0) {
                matchOverByBankrupt = true;
                lastWinResult.title = "【破产打飞・血战比赛结束】";
                statusMsg = "有人负分破产，比赛结束！按(R)重置50000分新开比赛 | (-)回大厅";
            } else {
                statusMsg = "血战结束！按(R)下一局 | (L+R)重置筹码 | (-)切换玩法";
            }
            hasWinPanel = true;
            phase = 3;
        } else {
            // 牌局继续！由胡牌者的下一位未胡玩家接力摸牌
            int nxt = nextActiveSeat(w);
            statusMsg = res.title + "！血战继续，轮到下一家...";
            if (nxt == 3) {
                playerHand.push_back(wall.back()); wall.pop_back();
                cursor = std::max(0, (int)playerHand.size() - 1);
                turn = 3; phase = 0; justDidGang = false;
            } else {
                turn = nxt; phase = 1; aiTimer = SDL_GetTicks();
            }
        }
    };
        auto playerDrawAndEnterTurn = [&](bool fromGang) {
                if (wall.empty()) {
            phase = 3;
            hasWinPanel = (gameMode == 2 && !xuezhanLogs.empty());
            if (hasWinPanel) {
                lastWinResult = {3, "四川血战到底・牌墙摸完总结算", xuezhanLogs.back(), {0, 0, 0, 0}, {}, 0};
            }
            statusMsg = "流局！牌堆已摸完，按(R)继续下一局";
            return;
        }
        playerHand.push_back(wall.back());
        wall.pop_back();
        cursor = std::max(0, (int)playerHand.size() - 1);
        turn = 3; phase = 0; justDidGang = fromGang;
        bool allow7 = (gameMode == 1 || gameMode == 2 || gameMode == 3);
        bool noVoidLeft = !hasVoidTiles(playerHand, voidSuit.at(3));
        if (playerRiichiDeclared) {
            statusMsg = "(立直中) 自动摸牌：按(A)摸切打出";
            if (canWinShape(playerHand, true, true)) statusMsg += " | 按(Y)立直自摸！";
        } else if (gameMode == 2 && !noVoidLeft) {
            statusMsg = "(四川定缺:缺" + suitNameCN(voidSuit.at(3)) + ") 请优先打出【" + suitNameCN(voidSuit.at(3)) + "】字牌！";
        } else {
            statusMsg = "你的回合：左右选牌，(A)出牌";
            for (const auto& m : playerMelds) {
                if (m.type == 1 && hasTileVal(playerHand, m.tiles.at(0).suit, m.tiles.at(0).val)) {
                    statusMsg += " | ★可加杠【" + m.tiles.at(0).label + "】按(L)开杠！";
                    break;
                }
            }
            if (noVoidLeft && canWinShape(playerHand, allow7, true)) statusMsg += " | 按(Y)自摸胡牌！";
        }
    };

    auto startNewGame = [&](bool resetAll) {
        int startChips = 50000;
        int initTiles = (gameMode == 0) ? 16 : 13;
        if (resetAll || matchOverByBankrupt) {
            playerScore = startChips; roundNum = 1; streakCount = 0;
            totalWins = 0; totalLosses = 0;
            matchOverByBankrupt = false;
            for (int i = 0; i < 3; ++i) ais.at(i).score = startChips;
        } else {
            roundNum++;
        }
        wall = buildWall(gameMode == 2); // 四川模式去掉字牌，只用 108 张万筒条
        if (gameMode == 1) {
            doraIndicator = wall.back(); wall.pop_back();
            actualDora = getNextDoraTile(doraIndicator);
        }
        playerHand.clear(); playerMelds.clear(); discards.clear(); xuezhanLogs.clear();
        hasWinPanel = false; justDidGang = false;
        playerRiichiDeclared = false; playerIppatsu = false;
        voidSuit = {-1, -1, -1, -1};
        hasWonSeat = {false, false, false, false};

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
        cursor = 0; turn = 3;

        if (gameMode == 2) {
            for (int i = 0; i < 3; ++i) {
                std::vector<int> sc = {0, 0, 0};
                for (const auto& t : ais.at(i).hand) if (t.suit < 3) sc.at(t.suit)++;
                int minS = 0;
                if (sc.at(1) < sc.at(minS)) minS = 1;
                if (sc.at(2) < sc.at(minS)) minS = 2;
                voidSuit.at(i) = minS;
                ais.at(i).bubble = "本局我定缺【" + suitNameCN(minS) + "】！";
            }
            std::vector<int> pSc = {0, 0, 0};
            for (const auto& t : playerHand) if (t.suit < 3) pSc.at(t.suit)++;
            dingQueCursor = 0;
            if (pSc.at(1) < pSc.at(dingQueCursor)) dingQueCursor = 1;
            if (pSc.at(2) < pSc.at(dingQueCursor)) dingQueCursor = 2;
            phase = 4; // 进入开局定缺选择弹窗
            statusMsg = "(四川血战到底) 请先选择本局定缺花色！";
        } else {
            phase = 0;
            if (gameMode == 0) statusMsg = "(台湾16张麻将) 选牌按(A)出牌";
            else if (gameMode == 1) statusMsg = "(日本立直麻将) 选牌按(A)出牌";
            else statusMsg = "(广东推倒胡・带买马) 选牌按(A)出牌";

            bool allow7 = (gameMode == 1 || gameMode == 3);
            if (canWinShape(playerHand, allow7, true)) statusMsg += " | 天胡！按(Y)胡牌！";
        }
    };
    std::function<bool(const Tile&, int)> checkPlayerReactions;
    auto tryAIReactions = [&](const Tile& t, int fromSeat) -> bool {
        bool allow7 = (gameMode == 1 || gameMode == 2 || gameMode == 3);
        for (int step = 1; step <= 3; ++step) {
            int seat = (fromSeat + step) % 4;
            if (seat == 3 || seat == fromSeat || hasWonSeat.at(seat)) continue;
            if (gameMode == 2 && hasVoidTiles(ais.at(seat).hand, voidSuit.at(seat))) continue;
            auto aiTest = ais.at(seat).hand;
            aiTest.push_back(t);
            if (canWinShape(aiTest, allow7, true)) {
                if (gameMode != 1 || checkHasValidRiichiYaku(aiTest, ais.at(seat).melds, false, false)) {
                    ais.at(seat).bubble = ais.at(seat).huLine;
                    playVoice(ais.at(seat).huVoice, gSfxHu, true);
                    handleSeatWin(evaluateWinFull(seat, ais.at(seat).name, aiTest, ais.at(seat).melds, gameMode, false, false,
                                                  false, false, actualDora, fromSeat, 0, wall), false, fromSeat);
                    return true;
                }
            }
        }
        for (int step = 1; step <= 3; ++step) {
            int seat = (fromSeat + step) % 4;
            if (seat == 3 || seat == fromSeat || hasWonSeat.at(seat)) continue;
            if (gameMode == 2 && t.suit == voidSuit.at(seat)) continue;
            AIPlayer& ai = ais.at(seat);
            int same = countTile(ai.hand, t);
            if (same >= 3 && !wall.empty()) {
                if (!discards.empty()) discards.pop_back();
                for (int k = 0; k < 3; ++k) removeOneTile(ai.hand, t.suit, t.val);
                ai.melds.push_back({2, {t, t, t, t}});
                ai.bubble = ai.gangLine;
                playVoice(ai.gangVoice, gSfxGang ? gSfxGang : gSfxPlay, true);
                applySichuanGangMoney(seat, false);
                ai.hand.push_back(wall.back()); wall.pop_back();
                if (!hasVoidTiles(ai.hand, voidSuit.at(seat)) && canWinShape(ai.hand, allow7, true)) {
                    ai.bubble = ai.huLine;
                    playVoice(ai.huVoice, gSfxHu, true);
                    handleSeatWin(evaluateWinFull(seat, ai.name, ai.hand, ai.melds, gameMode, true, true,
                                                  false, false, actualDora, -1, 0, wall), true, -1);
                    return true;
                }
                int dropIdx = pickAIDiscard(ai.hand, voidSuit.at(seat));
                Tile out = ai.hand.at(dropIdx);
                ai.hand.erase(ai.hand.begin() + dropIdx);
                discards.push_back(out);
                turn = nextActiveSeat(seat);
                if (!checkPlayerReactions(out, seat)) {
                    if (turn == 3) playerDrawAndEnterTurn(false);
                    else { phase = 1; aiTimer = SDL_GetTicks(); statusMsg = ai.name + " 抢杠并打出 (" + out.label + ")"; }
                }
                return true;
            }
            if (same >= 2 && ai.hand.size() >= 4) {
                if (!discards.empty()) discards.pop_back();
                removeOneTile(ai.hand, t.suit, t.val);
                removeOneTile(ai.hand, t.suit, t.val);
                ai.melds.push_back({1, {t, t, t}});
                ai.bubble = ai.pengLine;
                playVoice(ai.pengVoice, gSfxPeng ? gSfxPeng : gSfxPlay, true);
                int dropIdx = pickAIDiscard(ai.hand, voidSuit.at(seat));
                Tile out = ai.hand.at(dropIdx);
                ai.hand.erase(ai.hand.begin() + dropIdx);
                discards.push_back(out);
                turn = nextActiveSeat(seat);
                if (!checkPlayerReactions(out, seat)) {
                    if (turn == 3) playerDrawAndEnterTurn(false);
                    else { phase = 1; aiTimer = SDL_GetTicks(); statusMsg = ai.name + " 碰牌后打出 (" + out.label + ")"; }
                }
                return true;
            }
        }
        int nextSeat = (fromSeat + 1) % 4;
        if (gameMode != 2 && nextSeat != 3 && !hasWonSeat.at(nextSeat) && t.suit < 3 && ais.at(nextSeat).hand.size() >= 4) {
            AIPlayer& ai = ais.at(nextSeat);
            int v = t.val;
            int p1 = -1, p2 = -1;
            if (hasTileVal(ai.hand, t.suit, v - 2) && hasTileVal(ai.hand, t.suit, v - 1)) { p1 = v - 2; p2 = v - 1; }
            else if (hasTileVal(ai.hand, t.suit, v - 1) && hasTileVal(ai.hand, t.suit, v + 1)) { p1 = v - 1; p2 = v + 1; }
            else if (hasTileVal(ai.hand, t.suit, v + 1) && hasTileVal(ai.hand, t.suit, v + 2)) { p1 = v + 1; p2 = v + 2; }

            if (p1 != -1 && (std::rand() % 100) < 65) {
                if (!discards.empty()) discards.pop_back();
                removeOneTile(ai.hand, t.suit, p1);
                removeOneTile(ai.hand, t.suit, p2);
                std::vector<Tile> mt = {
                    {t.suit, p1, makeTileLabel(t.suit, p1)},
                    t,
                    {t.suit, p2, makeTileLabel(t.suit, p2)}
                };
                std::sort(mt.begin(), mt.end());
                ai.melds.push_back({0, mt});
                ai.bubble = ai.chiLine;
                playVoice(ai.chiVoice, gSfxChi ? gSfxChi : gSfxPlay, true);
                int dropIdx = pickAIDiscard(ai.hand);
                Tile out = ai.hand.at(dropIdx);
                ai.hand.erase(ai.hand.begin() + dropIdx);
                discards.push_back(out);
                turn = nextActiveSeat(nextSeat);
                if (!checkPlayerReactions(out, nextSeat)) {
                    if (turn == 3) playerDrawAndEnterTurn(false);
                    else { phase = 1; aiTimer = SDL_GetTicks(); statusMsg = ai.name + " 吃牌后打出 (" + out.label + ")"; }
                }
                return true;
            }
        }
        return false;
    };

    checkPlayerReactions = [&](const Tile& t, int fromSeat) -> bool {
        if (hasWonSeat.at(3)) return false;
        optHu = false; optGang = false; optPeng = false; optChis.clear();
        bool allow7 = (gameMode == 1 || gameMode == 2 || gameMode == 3);
        bool noVoidLeft = !hasVoidTiles(playerHand, voidSuit.at(3));
        auto testWin = playerHand;
        testWin.push_back(t);
        if (noVoidLeft && canWinShape(testWin, allow7, true)) {
            if (gameMode != 1 || checkHasValidRiichiYaku(testWin, playerMelds, false, playerRiichiDeclared)) optHu = true;
        }
        if (!playerRiichiDeclared && !(gameMode == 2 && t.suit == voidSuit.at(3))) {
            int same = countTile(playerHand, t);
            if (same >= 3) optGang = true;
            if (same >= 2) optPeng = true;
            if (gameMode != 2 && fromSeat == 2 && t.suit < 3) {
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

    logStep("9: entering main loop");
    bool loggedFirstFrame = false;

    while (appletMainLoop()) {
        if (!loggedFirstFrame) { logStep("10: loop running"); loggedFirstFrame = true; }
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);

        bool btnUp    = (kDown & (HidNpadButton_Up    | HidNpadButton_StickLUp)) != 0;
        bool btnDown  = (kDown & (HidNpadButton_Down  | HidNpadButton_StickLDown)) != 0;
        bool btnLeft  = (kDown & (HidNpadButton_Left  | HidNpadButton_StickLLeft)) != 0;
        bool btnRight = (kDown & (HidNpadButton_Right | HidNpadButton_StickLRight)) != 0;

        bool touchTapped = false;
        int touchX = -1, touchY = -1;
        HidTouchScreenState touchState = {0};
        hidGetTouchScreenStates(&touchState, 1);
        if (touchState.count > 0 && prevTouch.count == 0) {
            touchTapped = true;
            touchX = (int)touchState.touches[0].x;
            touchY = (int)touchState.touches[0].y;
        }
        prevTouch = touchState;

        if (!gBgmList.empty() && (!Mix_PlayingMusic() || (kDown & HidNpadButton_ZL))) playRandomBgm();

        if (kDown & HidNpadButton_Plus) {
            if (showSettings) saveVolumeConfig();
            showSettings = !showSettings;
        }

        if (showSettings) {
            if (btnUp) settingsCursor = (settingsCursor + 3) % 4;
            if (btnDown) settingsCursor = (settingsCursor + 1) % 4;
            int delta = 0;
            if (btnLeft) delta = -10;
            if (btnRight) delta = 10;

            if (delta != 0) {
                if (settingsCursor == 0) {
                    gVolBgm = std::max(0, std::min(100, gVolBgm + delta));
                    applyBgmVolume();
                } else if (settingsCursor == 1) {
                    gVolVoice = std::max(0, std::min(100, gVolVoice + delta));
                    playVoice(ais[0].lineVoices[0], gSfxTaunt);
                } else if (settingsCursor == 2) {
                    gVolSfx = std::max(0, std::min(100, gVolSfx + delta));
                    playSfx(gSfxPlay);
                }
                saveVolumeConfig();
            }
            if ((kDown & HidNpadButton_A) && settingsCursor == 3) {
                saveVolumeConfig();
                break;
            }
            if (kDown & HidNpadButton_B) { saveVolumeConfig(); showSettings = false; }

            SDL_SetRenderDrawColor(ren, 15, 55, 32, 255);
            SDL_RenderClear(ren);
            drawSettingsOverlay();
            SDL_RenderPresent(ren);
            continue;
        }

        if (inMenu) {
            if (btnLeft || btnRight) {
                menuCursor ^= 1;
                playSfx(gSfxPlay);
            }
            if (btnUp || btnDown) {
                menuCursor ^= 2;
                playSfx(gSfxPlay);
            }
            if (touchTapped) {
                for (int i = 0; i < 4; ++i) {
                    int col = i % 2, row = i / 2;
                    SDL_Rect cRect = {110 + col * 550, 135 + row * 275, 510, 245};
                    if (touchX >= cRect.x && touchX <= cRect.x + cRect.w && touchY >= cRect.y && touchY <= cRect.y + cRect.h) {
                        menuCursor = i;
                        kDown |= HidNpadButton_A;
                    }
                }
            }
            if (kDown & HidNpadButton_A) {
                gameMode = menuCursor;
                inMenu = false;
                playSfx(gSfxPeng, gSfxPlay);
                startNewGame(true);
            }

            SDL_SetRenderDrawColor(ren, 15, 55, 32, 255);
            SDL_RenderClear(ren);
            SDL_Color cGold = {251, 191, 36, 255}, cWhite = {255, 255, 255, 255};
            SDL_Color cDark = {20, 20, 20, 255}, cCyan = {103, 232, 249, 255};

            drawText(ren, fontTitle, "明星三缺一风格・全能麻将大厅", 390, 35, cGold);
            drawText(ren, fontSmall, "方向键选玩法 | (A)进入对局 | (ZL)换BGM | (+)音量设置 | 局中(-)回大厅", 250, 88, cWhite);

            struct ModeCard { std::string title, l1, l2, l3; };
            std::vector<ModeCard> cards = {
                {"1. 台湾 16 张麻将", "・起手 16 张，凑 5 组顺刻 + 1 对眼", "・经典台数制：底台/门清/自摸/连庄/清一色", "・初始筹码：50,000 分（负分破产结束）"},
                {"2. 日本 13 张立直麻将", "・起手 13 张，支持七对子、宝牌翻倍", "・门清听牌按(ZR)立直 + 一发，满贯役满结算", "・初始点数：50,000 点（负分击飞结束）"},
                {"3. 四川血战到底 (正式版)", "・108 张万筒条，起手定缺、刮风下雨收税", "・一家胡牌不结束，三家胡完才散场！", "・初始筹码：50,000 分（负分破产结束）"},
                {"4. 广东推倒胡 (买马版)", "・13 张快节奏平胡，支持七对/碰碰/清一色", "・胡牌自动翻 4 张买马牌，中马直接翻倍！", "・初始筹码：50,000 分（负分破产结束）"}
            };

            for (int i = 0; i < 4; ++i) {
                int col = i % 2, row = i / 2;
                SDL_Rect cRect = {110 + col * 550, 135 + row * 275, 510, 245};
                bool sel = (menuCursor == i);
                SDL_SetRenderDrawColor(ren, sel ? 254 : 20, sel ? 240 : 83, sel ? 138 : 45, 255);
                SDL_RenderFillRect(ren, &cRect);
                SDL_SetRenderDrawColor(ren, sel ? 251 : 45, sel ? 191 : 130, sel ? 36 : 80, 255);
                SDL_RenderDrawRect(ren, &cRect);

                drawText(ren, fontBig, cards[i].title, cRect.x + 28, cRect.y + 22, sel ? cDark : cGold);
                drawText(ren, fontSmall, cards[i].l1, cRect.x + 28, cRect.y + 78, sel ? cDark : cWhite);
                drawText(ren, fontSmall, cards[i].l2, cRect.x + 28, cRect.y + 118, sel ? cDark : cWhite);
                drawText(ren, fontSmall, cards[i].l3, cRect.x + 28, cRect.y + 178, sel ? cDark : cCyan);
            }

            SDL_RenderPresent(ren);
            continue;
        }

        if (kDown & HidNpadButton_Minus) { inMenu = true; continue; }
        if ((kDown & HidNpadButton_R) || (phase == 3 && touchTapped && touchY > 120)) startNewGame((kHeld & HidNpadButton_L) != 0);

        // phase == 4: 四川麻将开局定缺选择阶段
        if (phase == 4) {
        if (touchTapped) {
            for (int s = 0; s < 3; ++s) {
            SDL_Rect btn = {290 + 45 + s * 210, 210 + 108, 190, 82};
        if (touchX >= btn.x && touchX <= btn.x + btn.w && touchY >= btn.y && touchY <= btn.y + btn.h) {
            dingQueCursor = s;
            kDown |= HidNpadButton_A;
                    }
                }
            }
            if (btnLeft) { dingQueCursor = (dingQueCursor + 2) % 3; playSfx(gSfxPlay); }
            if (btnRight) { dingQueCursor = (dingQueCursor + 1) % 3; playSfx(gSfxPlay); }
            if (kDown & HidNpadButton_A) {
                voidSuit.at(3) = dingQueCursor;
                playSfx(gSfxPeng, gSfxPlay);
                phase = 0;
                bool noVoidLeft = !hasVoidTiles(playerHand, voidSuit.at(3));
                if (!noVoidLeft) {
                    statusMsg = "(已定缺" + suitNameCN(voidSuit.at(3)) + ") 请优先打出【" + suitNameCN(voidSuit.at(3)) + "】字牌！";
                } else {
                    statusMsg = "(已定缺" + suitNameCN(voidSuit.at(3)) + ") 手里无缺牌，选牌按(A)出牌！";
                    if (canWinShape(playerHand, true, true)) statusMsg += " | 天胡！按(Y)胡牌！";
                }
            }
        }
        bool allow7 = (gameMode == 1 || gameMode == 2 || gameMode == 3);
        if (phase == 0 && !playerHand.empty()) {
            if (touchTapped && touchX >= 430 && touchX <= 870 && touchY >= 458 && touchY <= 502) {
                kDown |= HidNpadButton_L;
            }
            if (touchTapped && touchY >= 505 && touchY <= 665) {
                int calcX = 30 + (int)playerMelds.size() * 170;
                bool hasDrawn = (playerHand.size() % 3 == 2);
                int avW = 1240 - calcX - (hasDrawn ? 18 : 0);
                int cW = std::min(64, avW / std::max(1, (int)playerHand.size()) - 4);
                for (int i = 0; i < (int)playerHand.size(); ++i) {
                    int gap = (hasDrawn && i == (int)playerHand.size() - 1) ? 18 : 0;
                    int tx1 = calcX + i * (cW + 4) + gap;
                    int tx2 = tx1 + cW + 4;
                    if (touchX >= tx1 && touchX <= tx2) {
                        if (cursor == i || playerRiichiDeclared) kDown |= HidNpadButton_A;
                        else { cursor = i; playSfx(gSfxPlay); }
                        break;
                    }
                }
            }
            if (!playerRiichiDeclared) {
                if (btnLeft) cursor = (cursor - 1 + (int)playerHand.size()) % (int)playerHand.size();
                if (btnRight) cursor = (cursor + 1) % (int)playerHand.size();
            } else {
                cursor = (int)playerHand.size() - 1;
            }

            bool noVoidLeft = !hasVoidTiles(playerHand, voidSuit.at(3));

            if (gameMode == 1 && !playerRiichiDeclared && (kDown & HidNpadButton_ZR)) {
                bool menQing = true;
                for (const auto& m : playerMelds) if (m.type != 3) menQing = false;
                auto tempHand = playerHand;
                tempHand.erase(tempHand.begin() + cursor);
                if (menQing && playerScore >= 1000 && !getTenpaiTiles(tempHand, true).empty()) {
                    playerRiichiDeclared = true; playerIppatsu = true; playerScore -= 1000;
                    playSfx(gSfxGang, gSfxPeng);
                    Tile out = playerHand.at(cursor);
                    playerHand.erase(playerHand.begin() + cursor);
                    discards.push_back(out);
                    std::sort(playerHand.begin(), playerHand.end());
                    if (!tryAIReactions(out, 3)) {
                        turn = nextActiveSeat(3); phase = 1; aiTimer = SDL_GetTicks();
                        statusMsg = "(立直宣言！) 投入1000点立直棒，等待牌友出牌...";
                    }
                } else {
                    statusMsg = "无法立直(需门前清、点数>=1000且打出当前牌后听牌)";
                }
            } else if ((kDown & HidNpadButton_Y) && noVoidLeft && canWinShape(playerHand, allow7, true)) {
                playVoice(gSfxHu, nullptr, true);
                handleSeatWin(evaluateWinFull(3, "你", playerHand, playerMelds, gameMode, true, justDidGang,
                                              playerRiichiDeclared, playerIppatsu, actualDora, -1, streakCount, wall), true, -1);
            } else if (!playerRiichiDeclared && (kDown & HidNpadButton_L)) {
                bool didGang = false;
                bool isAnGang = false;
                Tile gangTile{0, 1, ""};
                for (size_t i = 0; i < playerHand.size(); ++i) {
                    Tile cand = playerHand.at(i);
                    if (gameMode == 2 && cand.suit == voidSuit.at(3)) continue;
                    if (countTile(playerHand, cand) == 4) {
                        gangTile = cand;
                        didGang = true; isAnGang = true;
                        break;
                    }
                }
                if (didGang) {
                    for (int k = 0; k < 4; ++k) removeOneTile(playerHand, gangTile.suit, gangTile.val);
                    playerMelds.push_back({3, {gangTile, gangTile, gangTile, gangTile}});
                } else {
                    for (auto& m : playerMelds) {
                        Tile firstTile = m.tiles.at(0);
                        if (m.type == 1 && hasTileVal(playerHand, firstTile.suit, firstTile.val)) {
                            removeOneTile(playerHand, firstTile.suit, firstTile.val);
                            m.type = 2;
                            m.tiles = {firstTile, firstTile, firstTile, firstTile};
                            didGang = true; isAnGang = false;
                            break;
                        }
                    }
                }
                if (didGang) {
                    playVoice(gSfxGang, gSfxPlay, true);
                    applySichuanGangMoney(3, isAnGang);
                    playerDrawAndEnterTurn(true);
                    statusMsg = (gameMode == 2 ? "刮风下雨收税成功！" : "开杠补牌成功！") + std::string("请选择一张打出");
                }
            } else if (kDown & HidNpadButton_A) {
                Tile out = playerHand.at(cursor);
                if (gameMode == 2 && !noVoidLeft && out.suit != voidSuit.at(3)) {
                    statusMsg = "【四川规则】手里还有定缺【" + suitNameCN(voidSuit.at(3)) + "】，必须先打完定缺牌！";
                } else {
                    playerHand.erase(playerHand.begin() + cursor);
                    discards.push_back(out);
                    playSfx(gSfxPlay);
                    justDidGang = false;
                    if (playerRiichiDeclared) playerIppatsu = false;
                    std::sort(playerHand.begin(), playerHand.end());
                    if (cursor >= (int)playerHand.size()) cursor = std::max(0, (int)playerHand.size() - 1);

                    if (!tryAIReactions(out, 3)) {
                        turn = nextActiveSeat(3); phase = 1; aiTimer = SDL_GetTicks();
                        statusMsg = "等待牌友出牌...";
                    }
                }
            }
        } else if (phase == 1) {
            if (turn < 0 || turn >= 3 || hasWonSeat.at(turn)) {
                turn = nextActiveSeat(turn);
                if (turn == 3) playerDrawAndEnterTurn(false);
                else aiTimer = SDL_GetTicks();
            } else if (SDL_GetTicks() - aiTimer >= 1500) {
            if (wall.empty()) {
            phase = 3;
            hasWinPanel = (gameMode == 2 && !xuezhanLogs.empty());
            if (hasWinPanel) {
            lastWinResult = {3, "四川血战到底・牌墙摸完总结算", xuezhanLogs.back(), {0, 0, 0, 0}, {}, 0};
               }
            statusMsg = "流局！牌堆已摸完，按(R)继续下一局";
                } else {
                    AIPlayer& ai = ais.at(turn);
                    ai.hand.push_back(wall.back()); wall.pop_back();
                    bool aiDidSelfGang = false;
                    Tile aiGangTile{0, 1, ""};
                    for (size_t i = 0; i < ai.hand.size(); ++i) {
                        Tile cand = ai.hand.at(i);
                        if (gameMode == 2 && cand.suit == voidSuit.at(turn)) continue;
                        if (countTile(ai.hand, cand) == 4 && !wall.empty()) {
                            aiGangTile = cand;
                            aiDidSelfGang = true;
                            break;
                        }
                    }
                    if (aiDidSelfGang) {
                        for (int k = 0; k < 4; ++k) removeOneTile(ai.hand, aiGangTile.suit, aiGangTile.val);
                        ai.melds.push_back({3, {aiGangTile, aiGangTile, aiGangTile, aiGangTile}});
                        ai.bubble = ai.gangLine;
                        playVoice(ai.gangVoice, gSfxGang ? gSfxGang : gSfxPlay, true);
                        applySichuanGangMoney(turn, true);
                        ai.hand.push_back(wall.back()); wall.pop_back();
                    }
                    if (!aiDidSelfGang && !wall.empty()) {
                        for (auto& m : ai.melds) {
                            Tile ft = m.tiles.at(0);
                            if (m.type == 1 && hasTileVal(ai.hand, ft.suit, ft.val)) {
                                removeOneTile(ai.hand, ft.suit, ft.val);
                                m.type = 2; m.tiles = {ft, ft, ft, ft};
                                ai.bubble = ai.gangLine;
                                playVoice(ai.gangVoice, gSfxGang ? gSfxGang : gSfxPlay, true);
                                applySichuanGangMoney(turn, false);
                                ai.hand.push_back(wall.back()); wall.pop_back();
                                aiDidSelfGang = true; break;
                            }
                        }
                    }

                    bool aiNoVoid = !hasVoidTiles(ai.hand, voidSuit.at(turn));
                    if (aiNoVoid && canWinShape(ai.hand, allow7, true)) {
                        ai.bubble = ai.huLine;
                        playVoice(ai.huVoice, gSfxHu, true);
                        handleSeatWin(evaluateWinFull(turn, ai.name, ai.hand, ai.melds, gameMode, true, aiDidSelfGang,
                                                      false, false, actualDora, -1, 0, wall), true, -1);
                    } else {
                        int dropIdx = pickAIDiscard(ai.hand, voidSuit.at(turn));
                        Tile out = ai.hand.at(dropIdx);
                        ai.hand.erase(ai.hand.begin() + dropIdx);
                        discards.push_back(out);

                        if (!aiDidSelfGang) {
                            // 1. 荷官 100% 必报出打出的牌名（走独立音效通道，绝不与角色语音冲突）
                            Mix_Chunk* tv = ai.tileVoices[out.suit][out.val];
                            if (tv) {
                                Mix_VolumeChunk(tv, (gVolVoice * MIX_MAX_VOLUME) / 100);
                                Mix_PlayChannel(1, tv, 0);
                            } else {
                                playSfx(gSfxPlay);
                            }

                            // 2. 气泡与性格吐槽：70% 报牌名，30% 顺带吐槽一句（不打断荷官报牌）
                            if (std::rand() % 10 < 3) {
                                int lineIdx = std::rand() % (int)ai.lines.size();
                                ai.bubble = ai.lines.at(lineIdx);
                                playVoice(ai.lineVoices.at(lineIdx), nullptr, false);
                            } else {
                                if (turn == 0) ai.bubble = "打一张【" + out.label + "】~";
                                else if (turn == 1) ai.bubble = "出牌，【" + out.label + "】。";
                                else ai.bubble = "接招吧，【" + out.label + "】！";
                            }
                        }
                        int whoDiscarded = turn;
                        turn = nextActiveSeat(turn);

                        if (!checkPlayerReactions(out, whoDiscarded)) {
                            if (!tryAIReactions(out, whoDiscarded)) {
                                if (turn == 3) playerDrawAndEnterTurn(false);
                                else aiTimer = SDL_GetTicks();
                            }
                        }
                    }
                }
            }
        } else if (phase == 2) {
            if (touchTapped && touchY >= 240 && touchY <= 370) {
                if (touchX >= 820) kDown |= HidNpadButton_B;
                else if (optHu) kDown |= HidNpadButton_Y;
                else if (optGang) kDown |= HidNpadButton_L;
                else if (optPeng) kDown |= HidNpadButton_X;
                else if (!optChis.empty()) kDown |= HidNpadButton_A;
            }
            if ((kDown & HidNpadButton_Y) && optHu) {
                playerHand.push_back(lastDiscard);
                std::sort(playerHand.begin(), playerHand.end());
                if (lastDiscardFrom >= 0 && lastDiscardFrom < 3) {
                    ais.at(lastDiscardFrom).bubble = ais.at(lastDiscardFrom).loseLine;
                    playVoice(ais.at(lastDiscardFrom).loseVoice, gSfxHu);
                } else {
                    playVoice(gSfxHu, nullptr, true);
                }
                handleSeatWin(evaluateWinFull(3, "你", playerHand, playerMelds, gameMode, false, false,
                                              playerRiichiDeclared, playerIppatsu, actualDora, lastDiscardFrom, streakCount, wall), false, lastDiscardFrom);
            } else if ((kDown & HidNpadButton_L) && optGang) {
                discards.pop_back();
                for (int k = 0; k < 3; ++k) removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                playerMelds.push_back({2, {lastDiscard, lastDiscard, lastDiscard, lastDiscard}});
                playVoice(gSfxGang, gSfxPlay, true);
                applySichuanGangMoney(3, false);
                playerDrawAndEnterTurn(true);
                statusMsg = (gameMode == 2 ? "刮风明杠收税并补牌！" : "明杠成功并补牌！") + std::string("请打出一张牌");
            } else if ((kDown & HidNpadButton_X) && optPeng) {
                discards.pop_back();
                removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                removeOneTile(playerHand, lastDiscard.suit, lastDiscard.val);
                playerMelds.push_back({1, {lastDiscard, lastDiscard, lastDiscard}});
                playVoice(gSfxPeng, gSfxPlay, true);
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
                playVoice(gSfxChi, gSfxPlay, true);
                std::sort(playerHand.begin(), playerHand.end());
                cursor = 0; turn = 3; phase = 0; justDidGang = false;
                statusMsg = "吃牌成功！请选择一张牌打出";
            } else if (kDown & HidNpadButton_B) {
                if (!tryAIReactions(lastDiscard, lastDiscardFrom)) {
                    if (turn == 3) playerDrawAndEnterTurn(false);
                    else { phase = 1; aiTimer = SDL_GetTicks(); statusMsg = "等待牌友出牌..."; }
                }
            }
        }

        SDL_Color bgCol = {20, 83, 45, 255};
        if (gameMode == 1) bgCol = {18, 65, 75, 255};
        else if (gameMode == 2) bgCol = {75, 28, 32, 255};
        else if (gameMode == 3) bgCol = {28, 52, 88, 255};
        SDL_SetRenderDrawColor(ren, bgCol.r, bgCol.g, bgCol.b, 255);
        SDL_RenderClear(ren);

        SDL_Color cWhite = {255, 255, 255, 255}, cYellow = {253, 224, 71, 255};
        SDL_Color cDark = {20, 20, 20, 255}, cCyan = {103, 232, 249, 255}, cGold = {251, 191, 36, 255};

        drawText(ren, fontSmall, statusMsg, 25, 8, cYellow);
        std::string topRight = "第" + std::to_string(roundNum) + "局(胜" + std::to_string(totalWins) + "/负" + std::to_string(totalLosses) +
                               ") | 点数:" + std::to_string(playerScore) + " | 余牌:" + std::to_string(wall.size()) + " | (+)设置 | (-)大厅";
        drawText(ren, fontSmall, topRight, 540, 8, cGold);

        auto getAiAvatar = [&](int aiIdx) -> SDL_Texture* {
            SDL_Texture* cur = aiNormal.at(aiIdx);
            if (phase == 3 && hasWinPanel && lastWinResult.scoreDeltas.size() >= 4) {
                if (lastWinResult.scoreDeltas.at(aiIdx) > 0 && aiWin.at(aiIdx)) cur = aiWin.at(aiIdx);
                else if (lastWinResult.scoreDeltas.at(aiIdx) < 0 && aiLose.at(aiIdx)) cur = aiLose.at(aiIdx);
            }
            return cur;
        };

        {
            SDL_Rect topBox = {435, 36, 410, 84};
            SDL_SetRenderDrawColor(ren, 12, 48, 30, 255);
            SDL_RenderFillRect(ren, &topBox);

            SDL_Rect av = {topBox.x + 8, topBox.y + 6, 72, 72};
            if (getAiAvatar(1)) SDL_RenderCopy(ren, getAiAvatar(1), nullptr, &av);

            drawText(ren, fontSmall, ais.at(1).name + " (" + std::to_string(ais.at(1).score) + ")", topBox.x + 90, topBox.y + 6, cWhite);
            SDL_Rect bub = {topBox.x + 90, topBox.y + 36, 308, 40};
            SDL_SetRenderDrawColor(ren, 254, 240, 138, 255);
            SDL_RenderFillRect(ren, &bub);
            drawTextWrapped(ren, fontSmall, ais.at(1).bubble, bub.x + 10, bub.y + 8, 290, cDark);

            int cnt = (int)ais.at(1).hand.size();
            int tw = 22, th = 34;
            int startX = 640 - (cnt * (tw + 2)) / 2;
            for (int k = 0; k < cnt; ++k) drawTileBack3D(ren, {startX + k * (tw + 2), 126, tw, th}, 0);

            int mx = 855;
            for (const auto& m : ais.at(1).melds) {
                for (size_t k = 0; k < m.tiles.size(); ++k) {
                    SDL_Rect mr = {mx, 62, 26, 42};
                    if (m.type == 3 && (k == 0 || k == 3)) drawTileBack3D(ren, mr, 0);
                    else drawMahjongTile(ren, fontSmall, fontSmall, m.tiles.at(k), mr, false);
                    mx += 27;
                }
                mx += 6;
            }
        }

        {
            SDL_Rect leftBox = {14, 150, 198, 315};
            SDL_SetRenderDrawColor(ren, 12, 48, 30, 255);
            SDL_RenderFillRect(ren, &leftBox);

            SDL_Rect av = {leftBox.x + 53, leftBox.y + 12, 92, 92};
            if (getAiAvatar(2)) SDL_RenderCopy(ren, getAiAvatar(2), nullptr, &av);

            drawText(ren, fontSmall, "上家・暴躁大哥", leftBox.x + 28, leftBox.y + 116, cWhite);
            drawText(ren, fontSmall, "点数: " + std::to_string(ais.at(2).score), leftBox.x + 40, leftBox.y + 144, cGold);
            drawText(ren, fontSmall, "手牌: " + std::to_string(ais.at(2).hand.size()) + "张", leftBox.x + 48, leftBox.y + 172, cCyan);

            SDL_Rect bub = {leftBox.x + 10, leftBox.y + 208, 178, 92};
            SDL_SetRenderDrawColor(ren, 254, 240, 138, 255);
            SDL_RenderFillRect(ren, &bub);
            drawTextWrapped(ren, fontSmall, ais.at(2).bubble, bub.x + 10, bub.y + 18, 158, cDark);

            int cnt = (int)ais.at(2).hand.size();
            int tw = 34, th = 16;
            int startY = 315 - (cnt * (th + 2)) / 2;
            for (int k = 0; k < cnt; ++k) drawTileBack3D(ren, {220, startY + k * (th + 2), tw, th}, 1);

            int mx = 14, my = 472;
            for (const auto& m : ais.at(2).melds) {
                for (size_t k = 0; k < m.tiles.size(); ++k) {
                    SDL_Rect mr = {mx + (int)k * 27, my, 26, 42};
                    if (m.type == 3 && (k == 0 || k == 3)) drawTileBack3D(ren, mr, 0);
                    else drawMahjongTile(ren, fontSmall, fontSmall, m.tiles.at(k), mr, false);
                }
                my += 45;
            }
        }

        {
            SDL_Rect rightBox = {1068, 150, 198, 315};
            SDL_SetRenderDrawColor(ren, 12, 48, 30, 255);
            SDL_RenderFillRect(ren, &rightBox);

            SDL_Rect av = {rightBox.x + 53, rightBox.y + 12, 92, 92};
            if (getAiAvatar(0)) SDL_RenderCopy(ren, getAiAvatar(0), nullptr, &av);

            drawText(ren, fontSmall, "下家・甜美辣妹", rightBox.x + 28, rightBox.y + 116, cWhite);
            drawText(ren, fontSmall, "点数: " + std::to_string(ais.at(0).score), rightBox.x + 40, rightBox.y + 144, cGold);
            drawText(ren, fontSmall, "手牌: " + std::to_string(ais.at(0).hand.size()) + "张", rightBox.x + 48, rightBox.y + 172, cCyan);

            SDL_Rect bub = {rightBox.x + 10, rightBox.y + 208, 178, 92};
            SDL_SetRenderDrawColor(ren, 254, 240, 138, 255);
            SDL_RenderFillRect(ren, &bub);
            drawTextWrapped(ren, fontSmall, ais.at(0).bubble, bub.x + 10, bub.y + 18, 158, cDark);

            int cnt = (int)ais.at(0).hand.size();
            int tw = 34, th = 16;
            int startY = 315 - (cnt * (th + 2)) / 2;
            for (int k = 0; k < cnt; ++k) drawTileBack3D(ren, {1026, startY + k * (th + 2), tw, th}, 2);

            int mx = 1068, my = 472;
            for (const auto& m : ais.at(0).melds) {
                for (size_t k = 0; k < m.tiles.size(); ++k) {
                    SDL_Rect mr = {mx + (int)k * 27, my, 26, 42};
                    if (m.type == 3 && (k == 0 || k == 3)) drawTileBack3D(ren, mr, 0);
                    else drawMahjongTile(ren, fontSmall, fontSmall, m.tiles.at(k), mr, false);
                }
                my += 45;
            }
        }
        SDL_Rect river = {268, 168, 744, 298};
        SDL_SetRenderDrawColor(ren, 14, 58, 36, 255);
        SDL_RenderFillRect(ren, &river);
        SDL_SetRenderDrawColor(ren, 34, 120, 75, 255);
        SDL_RenderDrawRect(ren, &river);

        std::string riverTitle = "";
        if (gameMode == 0) riverTitle = "中央牌河 (台湾16张 | 当前连庄: " + std::to_string(streakCount) + ")";
        else if (gameMode == 1) riverTitle = "中央牌河 (宝牌指示: " + doraIndicator.label + " -> 宝牌: " + actualDora.label + ")";
        else if (gameMode == 2) {
            riverTitle = "四川血战 | 缺门: 上家[" + suitNameCN(voidSuit.at(2)) + "] 对家[" + suitNameCN(voidSuit.at(1)) +
                         "] 下家[" + suitNameCN(voidSuit.at(0)) + "] 你[" + suitNameCN(voidSuit.at(3)) + "]";
        } else riverTitle = "中央牌河 (广东推倒胡 | 1/5/9及东风中马翻倍)";
        drawText(ren, fontSmall, riverTitle, river.x + 16, river.y + 8, cWhite);

        // 四川血战模式：若已有玩家胡牌，在中央牌河右上角亮出金色“[已胡牌]”播报徽章
        if (gameMode == 2) {
            std::string wonBanner = "";
            if (hasWonSeat.at(0)) wonBanner += "[下家已胡] ";
            if (hasWonSeat.at(1)) wonBanner += "[对家已胡] ";
            if (hasWonSeat.at(2)) wonBanner += "[上家已胡] ";
            if (hasWonSeat.at(3)) wonBanner += "[你已胡] ";
            if (!wonBanner.empty()) drawText(ren, fontSmall, wonBanner, river.x + 16, river.y + 272, cGold);
        }

        int startIdx = std::max(0, (int)discards.size() - 52);
        for (int i = startIdx; i < (int)discards.size(); ++i) {
            int idx = i - startIdx;
            int col = idx % 13, row = idx / 13;
            SDL_Rect tRect = {river.x + 16 + col * 55, river.y + 36 + row * 64, 50, 56};
            drawMahjongTile(ren, fontSmall, fontSmall, discards.at(i), tRect, false);
        }

        // phase == 4: 四川血战开局定缺选择弹窗
        if (phase == 4) {
            SDL_Rect dqBox = {290, 210, 700, 220};
            SDL_SetRenderDrawColor(ren, 65, 18, 22, 248);
            SDL_RenderFillRect(ren, &dqBox);
            SDL_SetRenderDrawColor(ren, 251, 191, 36, 255);
            SDL_RenderDrawRect(ren, &dqBox);

            drawText(ren, fontBig, "四川血战到底：请选择本局【定缺花色】", dqBox.x + 115, dqBox.y + 20, cGold);
            drawText(ren, fontSmall, "左右方向键选择要弃用的花色，按 (A) 确认开始打牌", dqBox.x + 125, dqBox.y + 62, cCyan);

            std::vector<std::string> dqNames = {"定缺【万子】", "定缺【筒子】", "定缺【条子】"};
            for (int s = 0; s < 3; ++s) {
                int cntS = 0;
                for (const auto& t : playerHand) if (t.suit == s) cntS++;
                SDL_Rect btn = {dqBox.x + 45 + s * 210, dqBox.y + 108, 190, 82};
                bool sel = (dingQueCursor == s);
                SDL_SetRenderDrawColor(ren, sel ? 254 : 30, sel ? 240 : 60, sel ? 138 : 40, 255);
                SDL_RenderFillRect(ren, &btn);
                drawText(ren, fontBig, dqNames.at(s), btn.x + 25, btn.y + 14, sel ? cDark : cWhite);
                drawText(ren, fontSmall, "手牌有 " + std::to_string(cntS) + " 张", btn.x + 42, btn.y + 50, sel ? cDark : cGold);
            }
        }

        if (phase == 2) {
            SDL_Rect banner = {260, 260, 760, 88};
            SDL_SetRenderDrawColor(ren, 127, 29, 29, 240);
            SDL_RenderFillRect(ren, &banner);
            drawText(ren, fontBig, "打出(" + lastDiscard.label + ")！ " + statusMsg, banner.x + 25, banner.y + 28, cYellow);
        }

        if (phase == 3 && hasWinPanel) {
            int extraH = (gameMode == 2 && xuezhanLogs.size() > 1) ? std::min((int)xuezhanLogs.size(), 5) * 28 : 0;
            int panelH = (lastWinResult.horseTiles.empty() ? 240 : 300) + extraH;
            SDL_Rect panel = {210, 145, 860, panelH};
            SDL_SetRenderDrawColor(ren, lastWinResult.winnerSeat == 3 ? 120 : 40, 20, 20, 245);
            SDL_RenderFillRect(ren, &panel);
            SDL_SetRenderDrawColor(ren, 251, 191, 36, 255);
            SDL_RenderDrawRect(ren, &panel);

            drawText(ren, fontBig, lastWinResult.title, panel.x + 30, panel.y + 18, cGold);

            if (gameMode == 2 && !xuezhanLogs.empty()) {
                int showCnt = std::min((int)xuezhanLogs.size(), 5);
                for (int idx = 0; idx < showCnt; ++idx) {
                    drawText(ren, fontSmall, "• " + xuezhanLogs.at(idx), panel.x + 30, panel.y + 62 + idx * 30, idx % 2 == 0 ? cWhite : cCyan);
                }
            } else {
                drawText(ren, fontSmall, "番型明细：" + lastWinResult.details, panel.x + 30, panel.y + 62, cWhite);
                std::string deltaLine = "本局变动：你 " + (lastWinResult.scoreDeltas.at(3) >= 0 ? std::string("+") : std::string("")) + std::to_string(lastWinResult.scoreDeltas.at(3));
                for (int i = 0; i < 3; ++i) {
                    if (lastWinResult.scoreDeltas.at(i) != 0) {
                        std::string sgn = lastWinResult.scoreDeltas.at(i) >= 0 ? "+" : "";
                        deltaLine += " | " + ais.at(i).name + " " + sgn + std::to_string(lastWinResult.scoreDeltas.at(i));
                    }
                }
                drawText(ren, fontSmall, deltaLine, panel.x + 30, panel.y + 104, cCyan);
            }

            if (!lastWinResult.horseTiles.empty()) {
                drawText(ren, fontSmall, "翻开买马牌：", panel.x + 30, panel.y + 162, cGold);
                for (size_t h = 0; h < lastWinResult.horseTiles.size(); ++h) {
                    const Tile& ht = lastWinResult.horseTiles.at(h);
                    bool isHit = (ht.suit < 3 && (ht.val == 1 || ht.val == 5 || ht.val == 9)) || (ht.suit == 3 && ht.val == 1);
                    SDL_Rect hRect = {panel.x + 165 + (int)h * 68, panel.y + 142, 46, 62};
                    drawMahjongTile(ren, fontSmall, fontSmall, ht, hRect, isHit);
                    if (isHit) drawText(ren, fontSmall, "中!", hRect.x + 12, hRect.y + 64, cYellow);
                }
            }

            std::string panelHint = matchOverByBankrupt
                ? "【有人负分破产・比赛已结束】按(R)或点屏幕重置50,000分新开比赛 | 按(-)回大厅"
                : "按(R)或点屏幕继续下一局 | 按住(L+R)重置筹码 | 按(-)返回四模式大厅";
            drawText(ren, fontSmall, panelHint, panel.x + 30, panel.y + panelH - 42, cYellow);
        }

        std::string infoLine = "";
        if (playerRiichiDeclared) infoLine += "(已立直) ";
        if (phase == 0 && !playerRiichiDeclared) {
            std::string gangNotice = "";
            for (const auto& t : playerHand) {
                if (gameMode == 2 && t.suit == voidSuit.at(3)) continue;
                if (countTile(playerHand, t) == 4) {
                    gangNotice = "★ 手牌有四张【" + t.label + "】可暗杠！按(L)或点此开杠 ★";
                    break;
                }
            }
            for (const auto& m : playerMelds) {
                Tile firstTile = m.tiles.at(0);
                if (m.type == 1 && hasTileVal(playerHand, firstTile.suit, firstTile.val)) {
                    gangNotice = "★ 摸到碰牌【" + firstTile.label + "】可加杠！按(L)或点此开杠 ★";
                    break;
                }
            }
            if (!gangNotice.empty()) {
                SDL_Rect gBanner = {430, 460, 440, 40};
                SDL_SetRenderDrawColor(ren, 180, 35, 35, 245);
                SDL_RenderFillRect(ren, &gBanner);
                SDL_SetRenderDrawColor(ren, 253, 224, 71, 255);
                SDL_RenderDrawRect(ren, &gBanner);
                drawText(ren, fontSmall, gangNotice, gBanner.x + 14, gBanner.y + 9, cYellow);
            }
            if (gameMode == 1) {
                bool menQing = true;
                for (const auto& m : playerMelds) if (m.type != 3) menQing = false;
                auto tempHand = playerHand;
                if (!tempHand.empty()) tempHand.erase(tempHand.begin() + cursor);
                if (menQing && playerScore >= 1000 && !getTenpaiTiles(tempHand, true).empty()) {
                    infoLine += "(打出此牌听牌！可按ZR拍棒立直) ";
                }
            }
        } else if (phase == 1 || phase == 2) {
            auto waits = getTenpaiTiles(playerHand, allow7);
            if (!waits.empty()) {
                infoLine += "已听牌！胡：";
                for (size_t i = 0; i < waits.size(); ++i) infoLine += waits.at(i).label + (i + 1 < waits.size() ? "、" : "");
            }
        }
        if (!infoLine.empty()) drawText(ren, fontSmall, infoLine, 35, 476, cCyan);

        int drawX = 30;
        for (const auto& m : playerMelds) {
            for (size_t k = 0; k < m.tiles.size(); ++k) {
                bool hidden = (m.type == 3 && (k == 0 || k == 3));
                SDL_Rect mRect = {drawX, 565, 50, 82};
                if (hidden) drawTileBack3D(ren, mRect, 0);
                else drawMahjongTile(ren, fontSmall, fontSmall, m.tiles.at(k), mRect, false);
                drawX += 53;
            }
            drawX += 12;
        }

        bool hasDrawnTile = (phase == 0 && (playerHand.size() % 3 == 2));
        int availW = 1240 - drawX - (hasDrawnTile ? 18 : 0);
        int cardW = std::min(64, availW / std::max(1, (int)playerHand.size()) - 4);
        for (int i = 0; i < (int)playerHand.size(); ++i) {
            bool selected = (i == cursor && phase == 0);
            int extraGap = (hasDrawnTile && i == (int)playerHand.size() - 1) ? 18 : 0;
            SDL_Rect card = {drawX + i * (cardW + 4) + extraGap, selected ? 515 : 545, cardW, 105};
            drawMahjongTile(ren, fontBig, fontSmall, playerHand.at(i), card, selected);
        }

        // 胡牌金色闪屏光效（前 350 毫秒）
        if (gWinFlashTimer > 0 && SDL_GetTicks() - gWinFlashTimer < 350) {
            Uint8 alpha = (Uint8)(140 * (350 - (SDL_GetTicks() - gWinFlashTimer)) / 350);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 253, 224, 71, alpha);
            SDL_Rect flashRect = {0, 0, 1280, 720};
            SDL_RenderFillRect(ren, &flashRect);
        }

        // 更新并绘制胡牌漫天彩带金币粒子
        for (size_t i = 0; i < gParticles.size(); ) {
            auto& p = gParticles[i];
            p.x += p.vx;
            p.y += p.vy;
            p.vy += 0.25f;
            if (p.y > 740.0f) {
                gParticles.erase(gParticles.begin() + i);
            } else {
                SDL_SetRenderDrawColor(ren, p.col.r, p.col.g, p.col.b, 255);
                SDL_Rect pr = {(int)p.x, (int)p.y, p.w, p.h};
                SDL_RenderFillRect(ren, &pr);
                ++i;
            }
        }

        SDL_RenderPresent(ren);
    }
    saveVolumeConfig();
    if (fLog) fclose(fLog);
    Mix_HaltChannel(-1);
    Mix_HaltMusic();
    Mix_CloseAudio();
    SDL_Quit();
    romfsExit();
    if (appletGetAppletType() == AppletType_Application) {
        appletRequestExitToSelf();
    }
    return 0;
}

