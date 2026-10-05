void SimulationLoop::beginSession(GameSessionMode mode) {
    m_features.setSessionMode(mode);
    const auto& p = m_features.sessionParams;
    m_sessionType = toNetSessionType(mode);
    m_currentLap = 0;
    m_totalLaps = p.totalLaps;
    m_timeRemaining = p.sessionTimeSeconds > 0 ? (double)p.sessionTimeSeconds : 0.0;
    beginRaceSession();
}

void SimulationLoop::startFeatureServices(bool hostAnnounce) {
    m_features.startServices(hostAnnounce);
    m_features.onBeginSession = [this](GameSessionMode mode, const SessionStartParams&) {
        beginSession(mode);
    };
    m_features.onSetFlag = [this](uint8_t f) {
        RaceFlag rf = RaceFlag::Green;
        if (f == 2) rf = RaceFlag::Yellow;
        else if (f == 3) rf = RaceFlag::Yellow;
        else if (f >= 4) rf = RaceFlag::Checkered;
        setRaceFlag(rf);
    };
    m_features.onPenalty = [this](int car, int kind, float value, const std::string& reason) {
        using PT = Penalty::Type;
        PT ty = PT::TimeAdded;
        if (kind == 1) ty = PT::DriveThrough;
        else if (kind == 2) ty = PT::StopGo;
        m_raceSession.addPenalty(car, ty, value, reason);
    };
    m_features.onSetTimeOfDay = [this](float h) { setTimeOfDay(h); };
    m_features.onSetWeather = [this](const std::string& name) {
        auto wp = presetByName(name);
        ks::physics::WeatherState ws;
        ws.ambientTemp = wp.ambientC;
        ws.trackTemp = wp.trackC;
        ws.trackWetness = wp.wetness;
        ws.rainIntensity = wp.rain;
        ws.windSpeed = wp.windMs;
        ws.cloudCover = wp.cloud;
        setWeatherPreset(ws);
        m_features.weatherCtrl.applyPreset(name);
    };
    m_features.onSetupLoad = [this](const std::string& path) {
        if (!m_setupGarage) return;
        SetupData s = m_setupGarage->setup();
        loadSetupFromFile(s, path);
        m_setupGarage->setSetup(s);
    };
    m_features.onSetupSave = [this](const std::string& path) {
        if (!m_setupGarage) return;
        saveSetupToFile(m_setupGarage->setup(), path);
    };
}

bool SimulationLoop::loadReplayFile(const std::string& path) {
    return m_features.loadReplay(path);
}
