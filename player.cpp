#include "player.h"
#include <algorithm>
#include <random>

static libvlc_instance_t* createVlcInstance() {
    const char* argv[] = {"player", "-q"};
    return libvlc_new(2, argv);
}

Player::Player()
    : m_inst(createVlcInstance())
    , m_mp(m_inst ? libvlc_media_player_new(m_inst) : nullptr)
    , m_idx(-1)
    , m_shufPos(0)
    , m_playing(false)
    , m_paused(false)
    , m_vol(70)
    , m_repeat(0)
    , m_shuffle(false)
{
    if (m_mp) attach();
}

Player::~Player() {
    if (m_mp) {
        libvlc_media_player_stop(m_mp);
        libvlc_media_player_release(m_mp);
    }
    if (m_inst) libvlc_release(m_inst);
}

void Player::attach() {
    auto* em = libvlc_media_player_event_manager(m_mp);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached, onEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying, onEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerPaused, onEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerStopped, onEvent, this);
    libvlc_event_attach(em, libvlc_MediaPlayerMediaChanged, onEvent, this);
}

void Player::onEvent(const libvlc_event_t* ev, void* ptr) {
    auto* p = static_cast<Player*>(ptr);
    switch (ev->type) {
    case libvlc_MediaPlayerEndReached:
        p->m_needAdvance = 1;
        break;
    case libvlc_MediaPlayerPlaying:
        p->m_playing = true;
        p->m_paused = false;
        break;
    case libvlc_MediaPlayerPaused:
        p->m_playing = false;
        p->m_paused = true;
        break;
    case libvlc_MediaPlayerStopped:
        p->m_playing = false;
        p->m_paused = false;
        break;
    case libvlc_MediaPlayerMediaChanged: {
        if (!p->m_mp) break;
        auto* media = libvlc_media_player_get_media(p->m_mp);
        if (!media) break;
        const char* t = libvlc_media_get_meta(media, libvlc_meta_Title);
        if (t) {
            p->m_title = t;
        } else if (p->m_idx >= 0 && p->m_idx < (int)p->m_files.size()) {
            std::string path = p->m_files[p->m_idx];
            auto pos = path.find_last_of('/');
            if (pos != std::string::npos) path = path.substr(pos + 1);
            pos = path.find_last_of('.');
            if (pos != std::string::npos) path = path.substr(0, pos);
            p->m_title = path;
        }
        libvlc_media_release(media);
        break;
    }
    default: break;
    }
}

void Player::load(const std::vector<std::string>& files) {
    m_files = files;
    m_shuf.clear();
    m_shufPos = 0;
    m_idx = -1;
    if (m_shuffle && !m_files.empty()) {
        m_shuf.resize(m_files.size());
        for (size_t i = 0; i < m_files.size(); i++) m_shuf[i] = (int)i;
        std::shuffle(m_shuf.begin(), m_shuf.end(), std::mt19937(std::random_device()()));
    }
    if (!m_files.empty()) loadTrack(0);
}

void Player::loadTrack(int idx) {
    if (idx < 0 || idx >= (int)m_files.size()) return;
    if (!m_mp) return;
    m_idx = idx;

    auto* media = libvlc_media_new_path(m_inst, m_files[idx].c_str());
    if (!media) return;

    libvlc_media_parse_with_options(media, libvlc_media_parse_local, 2000);

    const char* t = libvlc_media_get_meta(media, libvlc_meta_Title);
    if (t) {
        m_title = t;
    } else {
        std::string path = m_files[idx];
        auto pos = path.find_last_of('/');
        if (pos != std::string::npos) path = path.substr(pos + 1);
        pos = path.find_last_of('.');
        if (pos != std::string::npos) path = path.substr(0, pos);
        m_title = path;
    }

    libvlc_media_player_set_media(m_mp, media);
    libvlc_media_release(media);
}

void Player::setFiles(const std::vector<std::string>& files) {
    std::string curPath;
    if (m_idx >= 0 && m_idx < (int)m_files.size())
        curPath = m_files[m_idx];

    m_files = files;
    m_shuf.clear();
    m_shufPos = 0;

    if (!curPath.empty()) {
        auto it = std::find(m_files.begin(), m_files.end(), curPath);
        if (it != m_files.end()) {
            m_idx = (int)(it - m_files.begin());
        } else {
            m_idx = -1;
            m_playing = false;
            m_paused = false;
            m_title.clear();
            if (m_mp) libvlc_media_player_stop(m_mp);
        }
    } else {
        m_idx = -1;
    }

    if (m_shuffle && !m_files.empty()) {
        m_shuf.resize(m_files.size());
        for (size_t i = 0; i < m_files.size(); i++) m_shuf[i] = (int)i;
        std::shuffle(m_shuf.begin(), m_shuf.end(), std::mt19937(std::random_device()()));
        if (m_idx >= 0) {
            auto it = std::find(m_shuf.begin(), m_shuf.end(), m_idx);
            if (it != m_shuf.end()) m_shufPos = (int)(it - m_shuf.begin());
        }
    }
}

void Player::play() {
    if (m_files.empty() || !m_mp) return;
    if (m_idx < 0) loadTrack(0);
    libvlc_media_player_play(m_mp);
}

void Player::pause() {
    if (!m_mp) return;
    libvlc_media_player_pause(m_mp);
}

void Player::toggle() {
    if (isPlaying()) pause();
    else play();
}

void Player::pollAdvance() {
    if (m_needAdvance.exchange(0))
        advance(1);
}

void Player::advance(int dir) {
    if (m_files.empty() || !m_mp) return;

    if (m_repeat == 1) {
        libvlc_media_player_set_time(m_mp, 0);
        libvlc_media_player_play(m_mp);
        return;
    }

    int next;
    if (m_shuffle && !m_shuf.empty()) {
        m_shufPos = (m_shufPos + dir + (int)m_shuf.size()) % m_shuf.size();
        next = m_shuf[m_shufPos];
    } else {
        next = (m_idx + dir + (int)m_files.size()) % m_files.size();
    }

    loadTrack(next);
    libvlc_media_player_play(m_mp);
}

void Player::next() { advance(1); }

void Player::prev() {
    if (!m_mp) return;
    if (position() > 3000) {
        libvlc_media_player_set_time(m_mp, 0);
        return;
    }
    advance(-1);
}

void Player::seekTo(int64_t ms) {
    if (!m_mp) return;
    libvlc_media_player_set_time(m_mp, ms);
}

void Player::setVolume(int vol) {
    if (!m_mp) return;
    m_vol = std::clamp(vol, 0, 100);
    libvlc_audio_set_volume(m_mp, m_vol);
}

void Player::setShuffle(bool s) {
    m_shuffle = s;
    if (s && !m_files.empty()) {
        m_shuf.resize(m_files.size());
        for (size_t i = 0; i < m_files.size(); i++) m_shuf[i] = (int)i;
        std::shuffle(m_shuf.begin(), m_shuf.end(), std::mt19937(std::random_device()()));
        m_shufPos = 0;
        auto it = std::find(m_shuf.begin(), m_shuf.end(), m_idx);
        if (it != m_shuf.end()) m_shufPos = (int)(it - m_shuf.begin());
    }
}

void Player::setRepeatMode(int m) { m_repeat = m; }

void Player::playIndex(int idx) {
    if (idx < 0 || idx >= (int)m_files.size()) return;
    if (!m_mp) return;
    if (m_shuffle && !m_shuf.empty()) {
        auto it = std::find(m_shuf.begin(), m_shuf.end(), idx);
        if (it != m_shuf.end()) m_shufPos = (int)(it - m_shuf.begin());
    }
    loadTrack(idx);
    libvlc_media_player_play(m_mp);
}

bool Player::isPlaying() const { return m_playing; }
bool Player::isPaused() const { return m_paused; }

int64_t Player::position() const {
    if (!m_mp) return 0;
    auto t = libvlc_media_player_get_time(m_mp);
    return t >= 0 ? t : 0;
}

int64_t Player::duration() const {
    if (!m_mp) return 0;
    auto t = libvlc_media_player_get_length(m_mp);
    return t >= 0 ? t : 0;
}
