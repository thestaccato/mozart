#include "player.h"
#include <ncurses.h>
#include <locale.h>
#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <csignal>
#include <clocale>

namespace fs = std::filesystem;

#define ICON_MUSIC     "\uf001"
#define ICON_STAR      "\uf005"
#define ICON_PLAY      "\uf04b"
#define ICON_PAUSE     "\uf04c"
#define ICON_STOP      "\uf04d"
#define ICON_PREV      "\uf048"
#define ICON_NEXT      "\uf051"
#define ICON_SHUFFLE   "\uf074"
#define ICON_REPEAT    "\uf01e"
#define ICON_VOL_UP    "\uf028"
#define ICON_FOLDER    "\uf07c"
#define ICON_PLUS      "\uf067"
#define ICON_SEARCH    "\uf002"

static volatile sig_atomic_t g_quitSig = 0;
static volatile sig_atomic_t g_winchSig = 0;

extern "C" void onSignal(int) { g_quitSig = 1; }
extern "C" void onWinch(int)  { g_winchSig = 1; }

static std::string cfgDir() {
    const char* h = getenv("HOME");
    return std::string(h ? h : "/tmp") + "/.config/mozart";
}

static std::string cfgFile(const char* name) {
    return cfgDir() + "/" + name;
}

static void ensureDir() {
    fs::create_directories(cfgDir());
}

static std::vector<std::string> loadLib() {
    std::vector<std::string> files;
    std::ifstream in(cfgFile("library"));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && fs::exists(line))
            files.push_back(line);
    }
    return files;
}

static void saveLib(const std::vector<std::string>& files) {
    ensureDir();
    std::ofstream out(cfgFile("library"));
    for (auto& f : files) out << f << '\n';
}

static std::set<std::string> loadStars() {
    std::set<std::string> stars;
    std::ifstream in(cfgFile("starred"));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) stars.insert(line);
    }
    return stars;
}

static void saveStars(const std::set<std::string>& stars) {
    ensureDir();
    std::ofstream out(cfgFile("starred"));
    for (auto& s : stars) out << s << '\n';
}

static void loadCfg(int& vol, bool& shuf, int& rep, int& lastTrack, int64_t& lastPos, bool& starredOnly) {
    std::ifstream in(cfgFile("config"));
    std::string line;
    while (std::getline(in, line)) {
        try {
            if (line.rfind("volume=", 0) == 0) vol = std::stoi(line.substr(7));
            else if (line.rfind("shuffle=", 0) == 0) shuf = (line.substr(8) == "true");
            else if (line.rfind("repeat=", 0) == 0) rep = std::stoi(line.substr(7));
            else if (line.rfind("last_track=", 0) == 0) lastTrack = std::stoi(line.substr(11));
            else if (line.rfind("last_position=", 0) == 0) lastPos = std::stoll(line.substr(14));
            else if (line.rfind("starred_only=", 0) == 0) starredOnly = (line.substr(13) == "true");
        } catch (...) {}
    }
}

static void saveCfg(int vol, bool shuf, int rep, bool starredOnly, int lastTrack, int64_t lastPos) {
    ensureDir();
    std::ofstream out(cfgFile("config"));
    out << "volume=" << vol << '\n';
    out << "shuffle=" << (shuf ? "true" : "false") << '\n';
    out << "repeat=" << rep << '\n';
    out << "starred_only=" << (starredOnly ? "true" : "false") << '\n';
    out << "last_track=" << lastTrack << '\n';
    out << "last_position=" << lastPos << '\n';
}

static std::string stripExt(const std::string& path) {
    auto pos = path.find_last_of('/');
    std::string name = (pos == std::string::npos) ? path : path.substr(pos + 1);
    pos = name.find_last_of('.');
    if (pos != std::string::npos) name = name.substr(0, pos);
    return name;
}

static std::string fmtTime(int64_t ms) {
    if (ms < 0) ms = 0;
    int s = (int)(ms / 1000);
    int m = s / 60; s %= 60;
    int h = m / 60; m %= 60;
    char buf[32];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else
        snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

static bool isAudio(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e == ".mp3" || e == ".wav" || e == ".flac" || e == ".ogg"
        || e == ".aac" || e == ".m4a" || e == ".wma" || e == ".opus";
}

static void scanDir(const std::string& path, std::vector<std::string>& out) {
    try {
        for (auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file() && isAudio(entry.path().extension().string())) {
                auto p = entry.path().string();
                if (std::find(out.begin(), out.end(), p) == out.end())
                    out.push_back(p);
            }
        }
    } catch (...) {}
}

static std::string promptInput(const char* msg) {
    nodelay(stdscr, FALSE);
    noecho();
    curs_set(1);
    mvprintw(LINES - 1, 0, "%s", msg);
    clrtoeol();
    std::string res;
    int ch;
    while ((ch = getch()) != ERR) {
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) break;
        if (ch == 27 || ch == 3) { res.clear(); break; }
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && !res.empty()) {
            res.pop_back();
            int x = (int)strlen(msg) + (int)res.size();
            mvaddch(LINES - 1, x, ' ');
            move(LINES - 1, x);
        } else if (ch >= 32 && ch < 127) {
            res += (char)ch;
            addch(ch);
        }
        refresh();
    }
    curs_set(0);
    timeout(33);
    return res;
}

static bool matchesQuery(const std::string& path, const std::string& query) {
    if (query.empty()) return true;
    std::string name = path;
    auto pos = name.find_last_of('/');
    if (pos != std::string::npos) name = name.substr(pos + 1);
    pos = name.find_last_of('.');
    if (pos != std::string::npos) name = name.substr(0, pos);
    auto it = std::search(
        name.begin(), name.end(),
        query.begin(), query.end(),
        [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }
    );
    return it != name.end();
}

static int colWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w++; i++; }
        else if (c < 0xC0) { i++; }
        else if (c < 0xE0) { w++; i += 2; }
        else if (c < 0xF0) { w++; i += 3; }
        else { w++; i += 4; }
    }
    return w;
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");

    struct sigaction sa;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = onSignal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = onWinch;
    sigaction(SIGWINCH, &sa, NULL);

    int volume = 70;
    bool shuffle = false;
    int repeat = 0;
    int lastTrack = -1;
    int64_t lastPos = 0;
    bool showStarredOnly = false;

    auto files = loadLib();
    auto starred = loadStars();
    loadCfg(volume, shuffle, repeat, lastTrack, lastPos, showStarredOnly);

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (fs::is_directory(arg)) {
            scanDir(arg, files);
        } else if (fs::is_regular_file(arg) && isAudio(fs::path(arg).extension().string())) {
            if (std::find(files.begin(), files.end(), arg) == files.end())
                files.push_back(arg);
        }
    }

    Player player;
    if (!files.empty()) {
        player.load(files);
        player.setVolume(volume);
        player.setShuffle(shuffle);
        player.setRepeatMode(repeat);
        if (lastTrack >= 0 && lastTrack < (int)files.size()) {
            player.playIndex(lastTrack);
            if (lastPos > 0) player.seekTo(lastPos);
        } else if (player.songCount() > 0) {
            player.playIndex(0);
        }
    }

    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    timeout(33);
    curs_set(0);
    bool hasColor = has_colors();
    if (hasColor) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_YELLOW, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_CYAN, -1);
        init_pair(4, COLOR_MAGENTA, -1);
    }

    int sel = std::clamp(lastTrack >= 0 ? lastTrack : 0, 0, std::max(0, (int)files.size() - 1));
    bool running = true;
    std::string status;
    int statusLife = 0;

    bool searchMode = false;
    std::string searchQuery;

    auto isVis = [&](int i) -> bool {
        if (i < 0 || i >= (int)files.size()) return false;
        if (showStarredOnly && !starred.count(files[i])) return false;
        if (!matchesQuery(files[i], searchQuery)) return false;
        return true;
    };

    auto nextVis = [&](int from, int dir) -> int {
        if (files.empty()) return 0;
        if (dir == 0) {
            if (isVis(from)) return from;
            for (int i = from + 1; i < (int)files.size(); i++)
                if (isVis(i)) return i;
            for (int i = from - 1; i >= 0; i--)
                if (isVis(i)) return i;
            return from;
        }
        int i = from + dir;
        while (i >= 0 && i < (int)files.size()) {
            if (isVis(i)) return i;
            i += dir;
        }
        return from;
    };

    auto clampSel = [&]() {
        if (files.empty()) { sel = 0; return; }
        int next = nextVis(sel, 0);
        if (!isVis(next)) {
            int first = nextVis(0, 1);
            sel = isVis(first) ? first : 0;
        } else {
            sel = next;
        }
    };

    int autoSaveTicker = 0;
    int prevTrack = -2;

    while (running) {
        player.pollAdvance();
        if (g_quitSig) { running = false; break; }
        if (g_winchSig) { endwin(); refresh(); g_winchSig = 0; }

        int curTrack = player.currentIndex();
        if (curTrack != prevTrack && curTrack >= 0 && prevTrack >= -1) {
            std::string name = stripExt(files[curTrack]);
            int maxW = 50;
            if ((int)name.size() > maxW) name = name.substr(0, maxW - 3) + "...";
            status = std::string(ICON_MUSIC) + " " + name;
            statusLife = 25;
        }
        prevTrack = curTrack;

        autoSaveTicker++;
        if (autoSaveTicker % 300 == 0) {
            int64_t pos = (player.isPlaying() || player.isPaused()) ? player.position() : 0;
            saveCfg(volume, shuffle, repeat, showStarredOnly, curTrack, pos);
            saveStars(starred);
            saveLib(files);
        }

        int ch = getch();
        if (ch != ERR) {
            if (searchMode) {
                if (ch == 27) {
                    searchMode = false;
                    searchQuery.clear();
                } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                    searchMode = false;
                } else if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && !searchQuery.empty()) {
                    searchQuery.pop_back();
                } else if (ch >= 32 && ch < 127) {
                    searchQuery += (char)ch;
                }
                sel = nextVis(sel, 0);
                if (!isVis(sel)) sel = nextVis(0, 1);
                if (!isVis(sel)) sel = 0;
            } else {
                switch (ch) {
                case 3: case 'q':
                    running = false;
                    break;
                case 27:
                    if (statusLife > 0) statusLife = 0;
                    break;
                case ' ':
                    if (player.songCount() > 0)
                        player.toggle();
                    break;
                case 'j': case KEY_DOWN:
                    sel = nextVis(sel, 1);
                    break;
                case 'k': case KEY_UP:
                    sel = nextVis(sel, -1);
                    break;
                case 'n':
                    if (player.songCount() > 0) { player.next(); status = std::string(ICON_NEXT) + " Next"; statusLife = 30; }
                    break;
                case 'p':
                    if (player.songCount() > 0) { player.prev(); status = std::string(ICON_PREV) + " Prev"; statusLife = 30; }
                    break;
                case 's':
                    shuffle = !shuffle;
                    player.setShuffle(shuffle);
                    status = std::string(ICON_SHUFFLE) + " " + (shuffle ? "ON" : "OFF");
                    statusLife = 30;
                    break;
                case 'r':
                    repeat = (repeat + 1) % 3;
                    player.setRepeatMode(repeat);
                    status = std::string(ICON_REPEAT) + " "
                        + (repeat == 0 ? "OFF" : (repeat == 1 ? "ONE" : "ALL"));
                    statusLife = 30;
                    break;
                case '+': case '=':
                    volume = std::min(100, volume + 5);
                    player.setVolume(volume);
                    status = std::string(ICON_VOL_UP) + " " + std::to_string(volume) + "%";
                    statusLife = 30;
                    break;
                case '-':
                    volume = std::max(0, volume - 5);
                    player.setVolume(volume);
                    status = std::string(ICON_VOL_UP) + " " + std::to_string(volume) + "%";
                    statusLife = 30;
                    break;
                case 'a': {
                    auto p = promptInput("Add directory: ");
                    if (p.empty()) break;
                    if (p[0] == '~') {
                        const char* h = getenv("HOME");
                        if (h) p = std::string(h) + p.substr(1);
                    }
                    {
                        std::error_code ec;
                        auto abs = fs::absolute(p, ec);
                        if (!ec) p = abs.string();
                    }
                    if (fs::is_directory(p)) {
                        int before = (int)files.size();
                        scanDir(p, files);
                        if ((int)files.size() > before) {
                            player.setFiles(files);
                            clampSel();
                            status = std::string(ICON_FOLDER) + " Added";
                            statusLife = 30;
                        } else {
                            status = "No new audio files";
                            statusLife = 20;
                        }
                    } else {
                        status = "Not a directory";
                        statusLife = 20;
                    }
                    break;
                }
                case 'd':
                    if (sel >= 0 && sel < (int)files.size()) {
                        bool wasCurrent = (sel == player.currentIndex());
                        starred.erase(files[sel]);
                        files.erase(files.begin() + sel);
                        player.setFiles(files);
                        if (wasCurrent && !files.empty()) {
                            int nextIdx = std::min(sel, (int)files.size() - 1);
                            player.playIndex(nextIdx);
                            sel = nextIdx;
                        } else {
                            clampSel();
                        }
                        status = "Deleted";
                        statusLife = 30;
                    }
                    break;
                case '*': case 'f':
                    if (sel >= 0 && sel < (int)files.size()) {
                        auto& path = files[sel];
                        if (starred.count(path)) {
                            starred.erase(path);
                            status = "Unstarred";
                        } else {
                            starred.insert(path);
                            status = std::string(ICON_STAR) + " Starred";
                        }
                        saveStars(starred);
                        if (!isVis(sel)) clampSel();
                        statusLife = 30;
                    }
                    break;
                case '\t':
                    showStarredOnly = !showStarredOnly;
                    clampSel();
                    status = showStarredOnly ? "Starred only" : "All songs";
                    statusLife = 25;
                    break;
                case '/':
                    searchMode = true;
                    searchQuery.clear();
                    statusLife = 0;
                    break;
                case 'g':
                    sel = nextVis(0, 0);
                    if (!isVis(sel)) sel = 0;
                    break;
                case 'G':
                    sel = nextVis((int)files.size() - 1, 0);
                    if (!isVis(sel)) sel = std::max(0, (int)files.size() - 1);
                    break;
                case '\n': case KEY_ENTER:
                    if (sel >= 0 && sel < (int)files.size()) {
                        player.playIndex(sel);
                        status = std::string(ICON_PLAY) + " Now playing";
                        statusLife = 30;
                    }
                    break;
                case KEY_LEFT:
                    if (player.isPlaying())
                        player.seekTo(std::max<int64_t>(0, player.position() - 5000));
                    break;
                case KEY_RIGHT:
                    if (player.isPlaying())
                        player.seekTo(player.position() + 5000);
                    break;
                case KEY_NPAGE: case 0x04: {
                    int half = std::max(1, (LINES - 12) / 2);
                    for (int i = 0; i < half; i++) sel = nextVis(sel, 1);
                    break;
                }
                case KEY_PPAGE: case 0x15: {
                    int half = std::max(1, (LINES - 12) / 2);
                    for (int i = 0; i < half; i++) sel = nextVis(sel, -1);
                    break;
                }
                case KEY_HOME:
                    sel = nextVis(0, 0);
                    if (!isVis(sel)) sel = 0;
                    break;
                case KEY_END:
                    sel = nextVis((int)files.size() - 1, 0);
                    if (!isVis(sel)) sel = std::max(0, (int)files.size() - 1);
                    break;
                }
            }
        }

        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        erase();

        bool hasSongs = player.songCount() > 0;
        int starCount = (int)starred.size();

        // Title Bar
        {
            attron(A_BOLD | (hasColor ? COLOR_PAIR(3) : 0));
            mvprintw(0, 1, "%s Mozart", ICON_MUSIC);
            attroff(A_BOLD | (hasColor ? COLOR_PAIR(3) : 0));

            std::string right;
            right += ICON_VOL_UP;
            right += " ";
            int v = player.volume();
            for (int i = 0; i < 10; i++)
                right += (i < v / 10) ? "\u2588" : "\u2591";
            right += " " + std::to_string(v) + "%";
            right += "  ";
            right += shuffle ? ICON_SHUFFLE : " ";
            right += " ";
            right += repeat == 0 ? " " : (repeat == 1 ? "1" : std::string(ICON_REPEAT));
            right += "  ";
            right += ICON_STAR;
            right += " " + std::to_string(starCount);

            if (hasColor) {
                attron(COLOR_PAIR(1));
                int rw = colWidth(right);
                if (rw < cols - 12)
                    mvprintw(0, cols - rw - 1, "%s", right.c_str());
                attroff(COLOR_PAIR(1));
            } else {
                int rw = colWidth(right);
                if (rw < cols - 12)
                    mvprintw(0, cols - rw - 1, "%s", right.c_str());
            }
        }

        // Separator
        mvhline(1, 0, ACS_HLINE, cols);

        // Now Playing
        int afterNP = 2;
        if (hasSongs) {
            std::string title = player.currentTitle();
            if (title.empty()) title = player.currentIndex() >= 0
                ? stripExt(files[player.currentIndex()]) : "(no title)";
            int maxW = cols - 8;
            if (colWidth(title) > maxW) {
                while (colWidth(title) > maxW - 3 && !title.empty())
                    title.pop_back();
                title += "...";
            }

            const char* stateIcon = player.isPlaying() ? ICON_PAUSE
                                   : (player.isPaused() ? ICON_PLAY : ICON_PLAY);

            if (hasColor) attron(COLOR_PAIR(2));
            mvprintw(2, 1, "%s  %s", stateIcon, title.c_str());
            if (hasColor) attroff(COLOR_PAIR(2));

            if (player.currentIndex() >= 0 && player.currentIndex() < (int)files.size()) {
                if (starred.count(files[player.currentIndex()])) {
                    if (hasColor) attron(COLOR_PAIR(1));
                    mvprintw(2, cols - 3, "%s", ICON_STAR);
                    if (hasColor) attroff(COLOR_PAIR(1));
                }
            }

            int64_t pos = player.position();
            int64_t dur = player.duration();
            if (dur > 0) {
                int barW = std::min(cols - 24, 55);
                if (barW > 8) {
                    int filled = (int)((double)pos / dur * barW);
                    std::string timeL = fmtTime(pos);
                    std::string timeR = fmtTime(dur);
                    std::string bar;
                    for (int i = 0; i < barW; i++)
                        bar += (i < filled) ? "\u2588" : "\u2591";
                    if (hasColor) attron(COLOR_PAIR(2));
                    mvprintw(3, 1, "%s %s %s", timeL.c_str(), bar.c_str(), timeR.c_str());
                    if (hasColor) attroff(COLOR_PAIR(2));
                }
            }
            afterNP = 4;
        } else {
            mvprintw(2, 1, "%s  No tracks in library", ICON_MUSIC);
            mvprintw(3, 1, "  Add with %s or: mozart ~/Music", ICON_FOLDER);
            afterNP = 4;
        }

        // Separator + Library Header
        if (afterNP < rows) {
            mvhline(afterNP, 0, ACS_HLINE, cols);
        }
        int hdrLine = afterNP + 1;
        int sepLine2 = afterNP + 2;

        // Library Info
        if (hdrLine < rows) {
            std::string label = "  Library";
            int totalVis = 0;
            int visPos = -1;
            for (int i = 0; i < (int)files.size(); i++) {
                if (!isVis(i)) continue;
                if (i == sel) visPos = totalVis;
                totalVis++;
            }
            if (totalVis > 0)
                label += " (" + std::to_string(totalVis) + ")";
            else
                label += " (0)";

            if (hasColor) attron(COLOR_PAIR(3) | A_DIM);
            else attron(A_DIM);
            mvprintw(hdrLine, 1, "%s", label.c_str());
            attroff(hasColor ? COLOR_PAIR(3) | A_DIM : A_DIM);

            // Right side info
            std::string rightInfo;
            if (showStarredOnly) {
                rightInfo += ICON_STAR;
                rightInfo += " filtered";
            }
            if (totalVis > 0 && visPos >= 0) {
                if (!rightInfo.empty()) rightInfo += "  ";
                rightInfo += std::to_string(visPos + 1) + "/" + std::to_string(totalVis);
            }
            if (!rightInfo.empty()) {
                int riw = colWidth(rightInfo);
                attron(A_DIM);
                mvprintw(hdrLine, cols - riw - 1, "%s", rightInfo.c_str());
                attroff(A_DIM);
            }
        }

        if (sepLine2 < rows) {
            mvhline(sepLine2, 0, ACS_HLINE, cols);
        }

        // ── Playlist ──
        int listStart = sepLine2 + 1;
        int listEnd = rows - 3;
        int avail = listEnd - listStart;

        std::vector<int> visible;
        int visPos = -1;
        for (int i = 0; i < (int)files.size(); i++) {
            if (!isVis(i)) continue;
            if (i == sel) visPos = (int)visible.size();
            visible.push_back(i);
        }

        int scrollOff = 0;
        if (visPos >= scrollOff + avail) scrollOff = visPos - avail + 1;
        if (visPos < scrollOff) scrollOff = visPos;
        if (scrollOff < 0) scrollOff = 0;

        // Scroll indicators
        bool hasAbove = scrollOff > 0;
        bool hasBelow = (int)visible.size() > scrollOff + avail;

        // Items
        for (int vi = scrollOff; vi < (int)visible.size() && vi < scrollOff + avail; vi++) {
            int y = listStart + vi - scrollOff;
            if (y >= rows) break;

            int i = visible[vi];
            std::string name = stripExt(files[i]);
            int maxW = cols - 8;
            if (colWidth(name) > maxW) {
                while (colWidth(name) > maxW - 3 && !name.empty())
                    name.pop_back();
                name += "...";
            }

            const char* star = starred.count(files[i]) ? ICON_STAR " " : "  ";
            const char* now = (i == player.currentIndex() && i >= 0) ? ICON_MUSIC : " ";

            if (i == sel) {
                attron(A_REVERSE);
                mvprintw(y, 1, "%s %s%s", now, star, name.c_str());
                clrtoeol();
                attroff(A_REVERSE);
            } else if (i == player.currentIndex() && i >= 0) {
                if (hasColor) attron(COLOR_PAIR(2) | A_BOLD);
                else attron(A_BOLD);
                mvprintw(y, 1, "%s %s%s", now, star, name.c_str());
                clrtoeol();
                attroff(hasColor ? COLOR_PAIR(2) | A_BOLD : A_BOLD);
            } else {
                mvprintw(y, 1, "%s %s%s", now, star, name.c_str());
                clrtoeol();
            }
        }

        // Scroll indicator arrows
        if (hasAbove && listStart < rows) {
            attron(A_DIM);
            mvprintw(listStart, cols - 3, "%s", "\u25B4");
            attroff(A_DIM);
        }
        int lastItemLine = listStart + std::min(avail, (int)visible.size() - scrollOff) - 1;
        if (hasBelow && lastItemLine >= 0 && lastItemLine < rows) {
            attron(A_DIM);
            mvprintw(lastItemLine, cols - 3, "%s", "\u25BE");
            attroff(A_DIM);
        }

        // Bottom separator
        int botSep = rows - 3;
        if (botSep >= 0) mvhline(botSep, 0, ACS_HLINE, cols);

        // Status Bar
        int infoLine = rows - 2;
        if (infoLine >= 0) {
            if (searchMode) {
                attron(A_REVERSE);
                mvprintw(infoLine, 1, "/%s", searchQuery.c_str());
                clrtoeol();
                attroff(A_REVERSE);
            } else if (statusLife > 0) {
                if (hasColor) attron(A_DIM | COLOR_PAIR(4));
                else attron(A_DIM);
                mvprintw(infoLine, 1, "%s", status.c_str());
                clrtoeol();
                attroff(hasColor ? A_DIM | COLOR_PAIR(4) : A_DIM);
                statusLife--;
            } else if (player.currentIndex() >= 0
                       && player.currentIndex() < (int)files.size()) {
                std::string name = stripExt(files[player.currentIndex()]);
                int maxW = cols - 4;
                if (colWidth(name) > maxW) {
                    while (colWidth(name) > maxW - 3 && !name.empty())
                        name.pop_back();
                    name += "...";
                }
                attron(A_DIM);
                mvprintw(infoLine, 1, "%s", name.c_str());
                clrtoeol();
                attroff(A_DIM);
            } else {
                clrtoeol();
            }
        }

        // Help
        int helpLine = rows - 1;
        if (helpLine >= 0) {
            std::string help;
            if (cols > 95) {
                help = "[Space]Play  [n/p]Prev  [s]Shuf  [r]Rep  [f]Star  [/]Find  [Tab]Filt  [a]Add  [d]Del  [g/G]Jump  [q]Quit";
            } else if (cols > 65) {
                help = "[Space]Play  [n/p]Prev  [s]Shuf  [r]Rep  [f]Star  [/]Find  [q]Quit";
            } else if (cols > 40) {
                help = "[Space]Play  [j/k]Nav  [s]Shuf  [r]Rep  [q]Quit";
            } else if (cols > 25) {
                help = "[Space]Play  [q]Quit";
            } else {
                help = "[/]  [q]";
            }
            attron(A_DIM);
            mvprintw(helpLine, 1, "%s", help.c_str());
            clrtoeol();
            attroff(A_DIM);
        }

        refresh();
    }

    int64_t finalPos = (player.isPlaying() || player.isPaused()) ? player.position() : 0;
    saveCfg(volume, shuffle, repeat, showStarredOnly, player.currentIndex(), finalPos);
    saveStars(starred);
    saveLib(files);
    endwin();
    return 0;
}
