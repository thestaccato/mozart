#pragma once
#include <vlc/vlc.h>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

class Player {
public:
    Player();
    ~Player();

    void load(const std::vector<std::string>& files);
    void play();
    void pause();
    void toggle();
    void next();
    void prev();
    void seekTo(int64_t ms);
    void setVolume(int vol);
    void setShuffle(bool s);
    void setRepeatMode(int m);
    void playIndex(int idx);
    void setFiles(const std::vector<std::string>& files);
    void pollAdvance();

    bool isPlaying() const;
    bool isPaused() const;
    int currentIndex() const { return m_idx; }
    int volume() const { return m_vol; }
    int64_t position() const;
    int64_t duration() const;
    const std::string& currentTitle() const { return m_title; }
    int songCount() const { return (int)m_files.size(); }
    const std::vector<std::string>& playlist() const { return m_files; }

private:
    static void onEvent(const libvlc_event_t*, void*);
    void attach();
    void advance(int dir);
    void loadTrack(int idx);

    libvlc_instance_t* m_inst;
    libvlc_media_player_t* m_mp;
    std::vector<std::string> m_files;
    std::vector<int> m_shuf;
    std::atomic<int> m_idx;
    int m_shufPos;
    std::atomic<bool> m_playing;
    std::atomic<bool> m_paused;
    int m_vol;
    int m_repeat;
    bool m_shuffle;
    std::string m_title;
    std::atomic<int> m_needAdvance{0};
};
