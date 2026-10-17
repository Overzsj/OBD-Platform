// Included by main.cpp after global objects. Acquisition task owns all commands.
static char g_usbJson[4096];
static void usbReply(uint32_t id,const char* type,bool ok,const char* message) {
    Serial.printf("USB|{\"id\":%lu,\"type\":\"%s\",\"ok\":%s,\"message\":\"%s\"}\n",(unsigned long)id,type,ok?"true":"false",message);
}
static void usbConfig(uint32_t id) {
    const auto& c=g_device.settings;
    Serial.printf("USB|{\"id\":%lu,\"type\":\"config\",\"ok\":true,\"rpm_warn\":%u,\"rpm_crit\":%u,\"temp_warn\":%u,\"temp_crit\":%u,\"muted\":%u,\"boot_animation\":%u,\"sound_level\":%u,\"storage\":%s,\"defaults_recovered\":%s}\n",
        (unsigned long)id,c.rpmWarn,c.rpmCrit,c.tempWarn,c.tempCrit,c.muted,c.bootAnimation,c.soundLevel,
        g_device.available?"true":"false",g_device.settingsRecovered?"true":"false");
}
static void usbView(uint32_t id,const char* message) {
    const uint8_t requested=g_requestedPage.load(),visible=g_visiblePage.load();
    Serial.printf("USB|{\"id\":%lu,\"type\":\"page\",\"ok\":true,\"message\":\"%s\",\"requested\":%u,\"visible\":%u,\"name\":\"%s\",\"simulated\":%s,\"touch_configured\":false,\"frames\":[%lu,%lu,%lu]}\n",
        (unsigned long)id,message,unsigned(requested),unsigned(visible),obd::pageName(obd::DisplayPage(visible)),obd::kSimulatedSource?"true":"false",
        (unsigned long)g_pageFrames[0].load(),(unsigned long)g_pageFrames[1].load(),(unsigned long)g_pageFrames[2].load());
}
static void usbEvents(uint32_t id) {
    auto& json=g_usbJson;size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%lu,\"type\":\"events\",\"ok\":true,\"pending_save\":%s,\"items\":[",(unsigned long)id,g_device.events.dirty?"true":"false");
    bool comma=false;
    auto append=[&](const obd::AlarmEvent& e,bool active) {
        pos+=snprintf(json+pos,sizeof(json)-pos,"%s{\"boot\":%lu,\"start_ms\":%lu,\"duration_ms\":%lu,\"alarm\":%u,\"severity\":%u,\"trigger\":%.1f,\"peak\":%.1f,\"rpm\":%.0f,\"quality\":%u,\"reason\":%u,\"active\":%s}",comma?",":"",(unsigned long)e.boot,(unsigned long)e.startMs,(unsigned long)e.durationMs,e.alarm,e.severity,e.trigger,e.peak,e.rpm,e.linkQuality,e.reason,active?"true":"false");comma=true;
    };
    for(uint32_t i=0;i<g_device.events.history.count;++i)append(g_device.events.history.rows[i],false);
    for(int i=0;i<2;++i)if(g_device.events.running[i])append(g_device.events.active[i],true);
    snprintf(json+pos,sizeof(json)-pos,"]}\n");Serial.print(json);
}
static void processUsb(const char* command,obd::VehicleSnapshot& s,uint32_t now) {
    unsigned id=0;int used=0;
    if(sscanf(command,"Q %u %n",&id,&used)!=1 || !used) {usbReply(0,"error",false,"bad_request");return;}
    const char* op=command+used;
    if(!strcmp(op,"DISPLAY GET")||!strncmp(op,"DISPLAY SET ",12)){
        if(op[8]=='S'){
            obd::DisplaySettings next;
            if(!obd::parseDisplay(op+12,next)){usbReply(id,"display",false,"invalid_display_ranges");return;}
            if(!g_device.saveDisplay(next)){usbReply(id,"display",false,"display_save_failed");return;}
            s.display=next;
        }
        Serial.printf("USB|{\"id\":%u,\"type\":\"display\",\"ok\":true,\"schema\":1,\"maximum\":[",id);
        for(uint8_t i=0;i<obd::kDisplaySignals;++i)Serial.printf("%s%u",i?",":"",g_device.display.maximum[i]);
        Serial.println("]}");return;
    }
#if OBD_DISPLAY_TFT
    if(!strcmp(op,"HARDWARE GET")){
        Serial.printf("USB|{\"id\":%u,\"type\":\"hardware\",\"ok\":true,\"board\":\"esp32-classic-4mb\",\"driver\":\"ILI9488\",\"width\":480,\"height\":320,\"rotation\":%u,\"engine\":\"suki-layout-v1\",\"spi_hz\":%u,\"pins\":{\"sclk\":%d,\"mosi\":%d,\"cs\":%d,\"dc\":%d,\"rst\":%d,\"buzzer\":%d},\"rpm_alarm_max\":%u}\n",id,TFT_ROTATION,unsigned(TFT_SPI_HZ),TFT_PIN_SCLK,TFT_PIN_MOSI,TFT_PIN_CS,TFT_PIN_DC,TFT_PIN_RST,OBD_BUZZER_PIN,unsigned(obd::kMaxRpmSetting));return;
    }
#endif
    if(!strcmp(op,"AUTO GET")){
        auto& json=g_usbJson;size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%u,\"type\":\"auto\",\"ok\":true,\"enabled\":%s,\"complete\":%s,\"vin\":\"%s\",\"protocol\":\"%s\",\"cache_match\":%s,\"signals\":[",id,OBD_AUTO_ADAPT?"true":"false",g_discovery.complete()?"true":"false",g_discovery.vin,g_discovery.protocol,g_discovery.cacheMatch?"true":"false");
        const uint8_t pids[]={0x0F,0x05,0x10,0x04,0x11,0x0C,0x0D,0x42,0x5C,0,0};
        for(uint8_t i=0;i<uint8_t(obd::Signal::Count);++i){const auto sample=obd::signalValue(s,obd::Signal(i),now);
            pos+=snprintf(json+pos,sizeof(json)-pos,"%s{\"id\":\"%s\",\"reported\":%d,\"status\":%u,\"value\":",i?",":"",obd::kSignals[i].id,int(g_discovery.reported(pids[i])),unsigned(sample.status));
            if(sample.fresh())pos+=snprintf(json+pos,sizeof(json)-pos,"%.3f",sample.value);else pos+=snprintf(json+pos,sizeof(json)-pos,"null");
            pos+=snprintf(json+pos,sizeof(json)-pos,"}");
        }snprintf(json+pos,sizeof(json)-pos,"]}\n");Serial.print(json);return;
    }
#if OBD_DISPLAY_TFT
    if(!strcmp(op,"THEME GET")){
        Serial.printf("USB|{\"id\":%u,\"type\":\"theme\",\"ok\":true,\"engine\":\"suki-layout-v1\",\"status\":\"%s\",\"theme\":\"%s\",\"slots\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",id,obd::g_runtimeTheme.status,obd::g_runtimeTheme.identity,obd::kSignals[uint8_t(obd::kThemeSlots[0])].id,obd::kSignals[uint8_t(obd::kThemeSlots[1])].id,obd::kSignals[uint8_t(obd::kThemeSlots[2])].id,obd::kSignals[uint8_t(obd::kThemeSlots[3])].id,obd::kSignals[uint8_t(obd::kThemeSlots[4])].id);return;
    }
#endif
    if(!strcmp(op,"LOG CLEAR")){const bool ok=g_journal.clearLogs();usbReply(id,"log_clear",ok,ok?"cleared":"clear_failed");return;}
    if(!strcmp(op,"LOG CHECK")){
        const int result=esp_spiffs_check(nullptr); // Arduino mounted the default partition without a label.
        Serial.printf("USB|{\"id\":%u,\"type\":\"log_check\",\"ok\":true,\"result\":%d,\"restart_required\":true}\n",id,result);return;
    }
    if(!strcmp(op,"LOG FILES")){
        Serial.printf("USB|{\"id\":%u,\"type\":\"log_files\",\"ok\":true,\"total\":%u,\"used\":%u,\"files\":[",id,unsigned(SPIFFS.totalBytes()),unsigned(SPIFFS.usedBytes()));
        File root=SPIFFS.open("/");File file=root.openNextFile();bool comma=false;
        while(file){Serial.printf("%s{\"name\":\"%s\",\"size\":%u}",comma?",":"",file.name(),unsigned(file.size()));comma=true;file.close();file=root.openNextFile();}
        root.close();Serial.println("]}");return;
    }
    if(!strcmp(op,"OPTIONS GET")||!strncmp(op,"OPTIONS SET ",12)){
        if(op[8]=='S'){
            unsigned layout,sounds;char extra;
            if(sscanf(op+12,"%u %u %c",&layout,&sounds,&extra)!=2||layout>1||sounds>7){usbReply(id,"options",false,"invalid_options");return;}
            obd::PresentationOptions next;next.layout=layout;next.sounds=sounds;
            if(!g_device.savePresentation(next)){usbReply(id,"options",false,"save_failed");return;}
            s.presentation=next;
        }
        Serial.printf("USB|{\"id\":%u,\"type\":\"options\",\"ok\":true,\"layout\":%u,\"startup_sound\":%s,\"rpm_sound\":%s,\"coolant_sound\":%s,\"sound_mask\":%u}\n",id,g_device.presentation.layout,g_device.presentation.sounds&1?"true":"false",g_device.presentation.sounds&2?"true":"false",g_device.presentation.sounds&4?"true":"false",g_device.presentation.sounds);return;
    }
    if(!strcmp(op,"BOOT PLAY")){
#if OBD_DISPLAY_TFT
        if(!s.settings.bootAnimation){usbReply(id,"boot",false,"enable_boot_animation_first");return;}
        g_requestedPage.store(0);g_bootReplayRequested.store(true);usbReply(id,"boot",true,"queued");
#else
        usbReply(id,"boot",false,"tft_required");
#endif
        return;
    }
#if OBD_DRIVE_REPLAY
    if(!strcmp(op,"REPLAY RESTART")){g_replay.begin(now);memset(g_alarmState,0,sizeof(g_alarmState));usbReply(id,"replay",true,"restarted");return;}
    if(!strcmp(op,"REPLAY PAUSE")){g_replay.pause(now);usbReply(id,"replay",true,"paused");return;}
    if(!strcmp(op,"REPLAY PLAY")){g_replay.play(now);usbReply(id,"replay",true,"playing");return;}
    if(!strcmp(op,"REPLAY GET")||!strcmp(op,"TELEMETRY")){
        uint8_t fresh=0;for(uint8_t i=0;i<obd::VF_COUNT;++i)if(obd::valueFresh(s,obd::ValueField(i),now))fresh|=1u<<i;
        Serial.printf("USB|{\"id\":%u,\"type\":\"telemetry\",\"ok\":true,\"simulated\":true,\"replay\":true,\"source_boot\":%lu,\"position_ms\":%lu,\"duration_ms\":%lu,\"row\":%lu,\"paused\":%s,\"ready\":%s,\"fresh_mask\":%u,\"alarm\":%u,\"values\":[",id,(unsigned long)obd::kReplaySourceBoot,(unsigned long)g_replay.position(now),(unsigned long)obd::kReplayDuration,(unsigned long)g_replay.index(),g_replay.paused()?"true":"false",s.linkUp?"true":"false",fresh,s.alarmFlags);
        const float values[]={s.rpm,s.oilTempC,s.coolantC,s.oilPressureBar,s.afr,s.throttle,s.speed,s.volts};
        for(uint8_t i=0;i<8;++i){if(fresh&(1u<<i))Serial.printf("%s%.3f",i?",":"",values[i]);else Serial.print(i?",null":"null");}
        Serial.print("],\"candidate_values\":[");
        for(uint8_t i=0;i<4;++i){if(obd::candidateFresh(s,i,now))Serial.printf("%s%.3f",i?",":"",s.candidateValues[i]);else Serial.print(i?",null":"null");}
        Serial.println("]}");return;
    }
#endif
    if(!strcmp(op,"CAN RUN")){
        if(obd::kSimulatedSource||!s.linkUp||g_scanOn||g_scanPending||!g_elm.caps().headersOff){usbReply(id,"can",false,"real_ready_link_required");return;}
        const bool ok=g_canQuery.start(now,g_device.boot);usbReply(id,"can",ok,ok?"started_read_only":"busy_or_storage_unavailable");return;
    }
    if(!strncmp(op,"CAN GET ",8)){
        unsigned index=0;char extra;
        if(sscanf(op+8,"%u %c",&index,&extra)!=1||index>=obd::kCanQueryCount){usbReply(id,"can",false,"invalid_index");return;}
        const auto r=g_canQuery.load(index);auto& json=g_usbJson;
        size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%u,\"type\":\"can_query\",\"ok\":true,\"active\":%s,\"step\":%u,\"run\":%lu,\"protocol6\":%s,\"restored\":%s,\"error\":%u,\"storage_ok\":%s,\"record\":{\"index\":%u,\"boot\":%lu,\"run\":%lu,\"uptime_ms\":%lu,\"prompt\":%s,\"truncated\":%s,\"raw_hex\":\"",id,g_canQuery.active()?"true":"false",g_canQuery.step(),(unsigned long)g_canQuery.run(),g_canQuery.protocol6?"true":"false",g_canQuery.restored?"true":"false",g_canQuery.error,g_canQuery.available&&g_canQuery.writeOk?"true":"false",index,(unsigned long)r.boot,(unsigned long)r.run,(unsigned long)r.ms,r.prompt?"true":"false",r.truncated?"true":"false");
        for(const char* p=r.raw;*p;++p)pos+=snprintf(json+pos,sizeof(json)-pos,"%02X",unsigned(uint8_t(*p)));
        snprintf(json+pos,sizeof(json)-pos,"\"}}\n");Serial.print(json);return;
    }
    if(!strncmp(op,"BUZZER ",7)){
        const char* names[]={"STOP","STARTUP","RPM","REDLINE"};
        for(uint8_t i=0;i<4;++i)if(!strcmp(op+7,names[i])){
            if(i&&s.settings.muted){usbReply(id,"buzzer",false,"muted_disable_mute_first");return;}
            if(i&&s.linkUp&&s.alarmFlags){usbReply(id,"buzzer",false,"real_alarm_active");return;}
            g_buzzerPreview.store(int8_t(i));
            Serial.printf("USB|{\"id\":%u,\"type\":\"buzzer\",\"ok\":true,\"queued\":true,\"preview\":\"%s\",\"pin\":%u,\"active_high\":true,\"affects_vehicle_data\":false}\n",id,names[i],unsigned(OBD_BUZZER_PIN));return;
        }
        usbReply(id,"buzzer",false,"invalid_preview");return;
    }
    if(!strcmp(op,"PROBE GET")||!strcmp(op,"PROBE GET 6")){
        const uint8_t start=!strcmp(op,"PROBE GET 6")?6:0;const uint8_t end=start?obd::kProbeCount:6;
        auto& json=g_usbJson;size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%u,\"type\":\"support_probe\",\"ok\":true,\"enabled\":%s,\"storage_ok\":%s,\"items\":[",id,OBD_SUPPORT_PROBE?"true":"false",g_probeArchive.available&&g_probeArchive.writeOk?"true":"false");
        for(uint8_t i=start;i<end;++i){const auto r=g_probeArchive.load(i);
            pos+=snprintf(json+pos,sizeof(json)-pos,"%s{\"query\":%u,\"boot\":%lu,\"uptime_ms\":%lu,\"state\":%u,\"prompt\":%s,\"truncated\":%s,\"value\":",i>start?",":"",i,(unsigned long)r.boot,(unsigned long)r.ms,unsigned(r.state),r.prompt?"true":"false",r.truncated?"true":"false");
            if(isfinite(r.value))pos+=snprintf(json+pos,sizeof(json)-pos,"%.3f",r.value);else pos+=snprintf(json+pos,sizeof(json)-pos,"null");
            pos+=snprintf(json+pos,sizeof(json)-pos,",\"raw_hex\":\"");
            for(const char* p=r.raw;*p&&pos+6<sizeof(json);++p)pos+=snprintf(json+pos,sizeof(json)-pos,"%02X",unsigned(uint8_t(*p)));
            pos+=snprintf(json+pos,sizeof(json)-pos,"\"}");
        }snprintf(json+pos,sizeof(json)-pos,"]}\n");Serial.print(json);return;
    }
    if(!strcmp(op,"TRACE GET")){
        // Encode one field at a time, keeping the existing 4 KB shared buffer.
        auto& json=g_usbJson;size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%u,\"type\":\"response_trace\",\"ok\":true,\"storage_ok\":%s,\"items\":[",id,g_responseArchive.available&&g_responseArchive.writeOk?"true":"false");
        for(uint8_t i=0;i<4;++i){const auto r=g_responseArchive.load(i);
            pos+=snprintf(json+pos,sizeof(json)-pos,"%s{\"boot\":%lu,\"uptime_ms\":%lu,\"pid\":%u,\"reason\":%u,\"prompt\":%s,\"truncated\":%s,\"previous\":",i?",":"",(unsigned long)r.boot,(unsigned long)r.ms,r.pid,r.reason,r.prompt?"true":"false",r.truncated?"true":"false");
            if(isfinite(r.previous))pos+=snprintf(json+pos,sizeof(json)-pos,"%.3f",r.previous);else pos+=snprintf(json+pos,sizeof(json)-pos,"null");
            pos+=snprintf(json+pos,sizeof(json)-pos,",\"value\":");
            if(isfinite(r.value))pos+=snprintf(json+pos,sizeof(json)-pos,"%.3f",r.value);else pos+=snprintf(json+pos,sizeof(json)-pos,"null");
            pos+=snprintf(json+pos,sizeof(json)-pos,",\"command_hex\":\"");
            for(const char* p=r.command;*p;++p)pos+=snprintf(json+pos,sizeof(json)-pos,"%02X",unsigned(uint8_t(*p)));
            pos+=snprintf(json+pos,sizeof(json)-pos,"\",\"raw_hex\":\"");
            for(const char* p=r.raw;*p;++p)pos+=snprintf(json+pos,sizeof(json)-pos,"%02X",unsigned(uint8_t(*p)));
            pos+=snprintf(json+pos,sizeof(json)-pos,"\"}");
        }snprintf(json+pos,sizeof(json)-pos,"]}\n");Serial.print(json);return;
    }
    if(!strcmp(op,"LOG STATUS")){
        Serial.printf("USB|{\"id\":%u,\"type\":\"log_status\",\"ok\":true,\"available\":%s,\"enabled\":%s,\"error\":\"%s\",\"records\":%lu,\"oldest\":%lu,\"last_seq\":%lu,\"pending\":%u,\"dropped\":%lu,\"sample_ms\":1000,\"flush_ms\":5000,\"max_records\":%u,\"schema\":2,\"segment_ms\":60000,\"segments\":2,\"simulation_recording\":false}\n",id,g_journal.available?"true":"false",g_journal.enabled?"true":"false",g_journal.error,(unsigned long)g_journal.records(),(unsigned long)g_journal.oldest(),(unsigned long)(g_journal.nextSeq-1),g_journal.pending,(unsigned long)g_journal.dropped,unsigned(480));return;
    }
    if(!strcmp(op,"LOG ENABLE 0")||!strcmp(op,"LOG ENABLE 1")){bool ok=g_journal.setEnabled(op[11]=='1');usbReply(id,"log",ok,ok?"saved":"save_failed");return;}
    if(!strcmp(op,"LOG FLUSH")){bool ok=g_journal.flush();usbReply(id,"log",ok,ok?"flushed":"flush_failed");return;}
    if(!strncmp(op,"LOG READ ",9)){
        unsigned long after=0;unsigned limit=0;char extra;
        if(sscanf(op+9,"%lu %u %c",&after,&limit,&extra)!=2||limit<1||limit>8){usbReply(id,"log",false,"invalid_page");return;}
        if(!g_journal.available){usbReply(id,"log",false,g_journal.error);return;}
        auto& json=g_usbJson;size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%u,\"type\":\"log\",\"ok\":true,\"items\":[",id);
        g_journal.read(after,limit,[&](const obd::DriveRecordV2& r,uint32_t row){
            pos+=snprintf(json+pos,sizeof(json)-pos,"%s{\"seq\":%lu,\"boot\":%lu,\"uptime_ms\":%lu,\"heap\":%lu,\"ok_count\":%lu,\"fail_count\":%lu,\"alarm\":%u,\"fresh_mask\":%u,\"quality\":%u,\"state\":%u,\"adapter_state\":%u,\"reason\":%u,\"values\":[",row?",":"",(unsigned long)r.seq,(unsigned long)r.boot,(unsigned long)r.uptimeMs,(unsigned long)r.heap,(unsigned long)r.ok,(unsigned long)r.fail,r.alarms,r.fresh,r.quality,r.state,r.adapter,r.reason);
            for(uint8_t i=0;i<8;++i){if((r.fresh&(1u<<i))&&isfinite(r.values[i]))pos+=snprintf(json+pos,sizeof(json)-pos,"%s%.3f",i?",":"",r.values[i]);else pos+=snprintf(json+pos,sizeof(json)-pos,"%snull",i?",":"");}
            pos+=snprintf(json+pos,sizeof(json)-pos,"],\"schema\":%u,\"candidate_mask\":%u,\"main_candidates\":[",r.version,r.candidateMask);
            for(uint8_t i=0;i<3;++i){if((r.candidateMask&(1u<<i))&&isfinite(r.candidates[i]))pos+=snprintf(json+pos,sizeof(json)-pos,"%s%.3f",i?",":"",r.candidates[i]);else pos+=snprintf(json+pos,sizeof(json)-pos,"%snull",i?",":"");}
            pos+=snprintf(json+pos,sizeof(json)-pos,"]}");
        });
        snprintf(json+pos,sizeof(json)-pos,"],\"storage_ok\":%s,\"error\":\"%s\"}\n",g_journal.available?"true":"false",g_journal.error);Serial.print(json);return;
    }
#if OBD_DISPLAY_TFT
    if(!strcmp(op,"BOOT")){Serial.printf("USB|{\"id\":%u,\"type\":\"boot_animation\",\"ok\":true,\"frames\":%lu,\"render_us\":%lu,\"wire_bytes\":%lu,\"last_ms\":%lu,\"active\":%s,\"target_frame_ms\":16}\n",id,(unsigned long)g_bootFrames.load(),(unsigned long)g_bootRenderUs.load(),(unsigned long)g_bootBytes.load(),(unsigned long)g_bootLastMs.load(),g_bootActive.load()?"true":"false");return;}
#endif
#if OBD_MULTI_ADAPTER
    if(!strcmp(op,"ADAPTER GET")) {
        const auto& c=g_tp.store.config;char ssidHex[65]{};for(size_t i=0;i<strlen(c.ssid);++i)snprintf(ssidHex+i*2,3,"%02x",uint8_t(c.ssid[i]));
        Serial.printf("USB|{\"id\":%u,\"type\":\"adapter\",\"ok\":true,\"active\":\"%s\",\"configured\":\"%s\",\"restart_required\":%s,\"ssid_hex\":\"%s\",\"password_set\":%s,\"host\":\"%s\",\"port\":%u,\"mac\":\"%s\",\"pin_set\":%s,\"service\":\"%s\",\"rx\":\"%s\",\"tx\":\"%s\",\"storage\":%s,\"catalog_names\":%u,\"ble_profiles\":3,\"usb_host\":false,\"mfi\":false}\n",id,obd::adapterModeName(g_tp.mode()),obd::adapterModeName(c.mode),g_tp.restartRequired()?"true":"false",ssidHex,*c.password?"true":"false",c.host,c.port,c.mac,*c.pin?"true":"false",c.service,c.rx,c.tx,g_tp.store.available?"true":"false",unsigned(sizeof(obd::adapters::names)/sizeof(obd::adapters::names[0])));return;
    }
    if(!strncmp(op,"ADAPTER SET ",12)) {
        char field[16]{},hex[256]{},extra;int fields=sscanf(op+12,"%15s %255s %c",field,hex,&extra);
        if(fields!=2||!g_tp.store.field(field,hex)){usbReply(id,"adapter",false,"invalid_adapter_setting_or_save_failed");return;}
        usbReply(id,"adapter",true,"saved_restart_required");return;
    }
    if(!strcmp(op,"ADAPTER RESTART")){usbReply(id,"adapter",true,"restarting");g_adapterRestartAt.store(millis()+500);return;}
#endif
#if OBD_DISPLAY_TFT
    if(!strcmp(op,"LIGHTS")) {
        Serial.printf("USB|{\"id\":%u,\"type\":\"lights\",\"ok\":true,\"frames\":%lu,\"flash_millihz\":%lu,\"target_frame_ms\":16,\"page\":%u}\n",id,(unsigned long)g_lightFrames.load(),(unsigned long)g_lightMilliHz.load(),unsigned(g_visiblePage.load()));return;
    }
#endif
#if OBD_DATA_TEST
    if(!strcmp(op,"TELEMETRY")) {
        const float values[]={s.rpm,s.oilTempC,s.coolantC,s.oilPressureBar,s.afr,s.throttle,s.speed,s.volts};
        char json[1400];size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%u,\"type\":\"telemetry\",\"ok\":true,\"simulated\":true,\"stage\":%u,\"phase_ms\":%lu,\"cycle\":%lu,\"uptime_ms\":%lu,\"ready\":%s,\"valid_mask\":%u,\"alarm\":%u,\"reinits\":%lu,\"events\":%lu,\"fresh_mask\":",id,g_tp.labStage(now),(unsigned long)g_tp.labPhaseMs(now),(unsigned long)g_tp.labCycle(now),(unsigned long)now,s.linkUp?"true":"false",s.validMask,s.alarmFlags,(unsigned long)s.reinits,(unsigned long)g_device.events.history.count);
        uint8_t fresh=0;for(uint8_t i=0;i<obd::VF_COUNT;++i)if(obd::valueFresh(s,obd::ValueField(i),now)&&isfinite(values[i]))fresh|=1u<<i;
        pos+=snprintf(json+pos,sizeof(json)-pos,"%u,\"values\":[",fresh);
        for(uint8_t i=0;i<obd::VF_COUNT;++i) {
            if(fresh&(1u<<i))pos+=snprintf(json+pos,sizeof(json)-pos,"%s%.3f",i?",":"",values[i]);
            else pos+=snprintf(json+pos,sizeof(json)-pos,"%snull",i?",":"");
        }
        pos+=snprintf(json+pos,sizeof(json)-pos,"],\"candidate_values\":[");
        for(uint8_t i=0;i<4;++i){if(obd::candidateFresh(s,i,now))pos+=snprintf(json+pos,sizeof(json)-pos,"%s%.3f",i?",":"",s.candidateValues[i]);else pos+=snprintf(json+pos,sizeof(json)-pos,"%snull",i?",":"");}
        pos+=snprintf(json+pos,sizeof(json)-pos,"],\"age_ms\":[");
        for(uint8_t i=0;i<obd::VF_COUNT;++i)pos+=snprintf(json+pos,sizeof(json)-pos,"%s%lu",i?",":"",(unsigned long)(now-s.valueMs[i]));
        snprintf(json+pos,sizeof(json)-pos,"]}\n");Serial.print(json);return;
    }
    if(!strncmp(op,"SIM ",4)) {
        int stage=-1;
        if(strcmp(op,"SIM AUTO")) {
            const char* number=op+10;
            if(strncmp(op,"SIM STAGE ",10) || strlen(op)<11 || strlen(op)>12) {usbReply(id,"sim",false,"invalid_stage");return;}
            stage=0;while(*number) {if(*number<'0'||*number>'9'){usbReply(id,"sim",false,"invalid_stage");return;}stage=stage*10+*number++-'0';}
            if(stage>=int(obd::kLabScenarioCount)){usbReply(id,"sim",false,"invalid_stage");return;}
        }
        g_tp.labControl(stage,now);
        g_elm.resetSimulatedTransaction();g_sched.begin();s.linkUp=false;s.validMask=0;s.alarmFlags=0;
        memset(g_alarmState,0,sizeof(g_alarmState));
#if OBD_SUPPORT_PROBE
        g_support.cancel();g_discovery.cancel();
        if(g_elm.state()==obd::ElmState::Ready)g_support.start(now,g_device.boot,200);
        s.adapterVoltsValid=false;
        memset(s.candidateStates,0,sizeof(s.candidateStates));
#endif
        usbReply(id,"sim",true,"simulated_only");return;
    }
#endif
#if !OBD_DATA_TEST
    if(!strcmp(op,"TELEMETRY")){
        const float values[]={s.rpm,s.oilTempC,s.coolantC,s.oilPressureBar,s.afr,s.throttle,s.speed,s.volts};
        uint8_t fresh=0;for(uint8_t i=0;i<obd::VF_COUNT;++i)if(obd::valueFresh(s,obd::ValueField(i),now)&&isfinite(values[i]))fresh|=1u<<i;
        auto& json=g_usbJson;size_t pos=snprintf(json,sizeof(json),"USB|{\"id\":%u,\"type\":\"telemetry\",\"ok\":true,\"simulated\":false,\"boot_id\":%lu,\"uptime_ms\":%lu,\"ready\":%s,\"fresh_mask\":%u,\"alarm\":%u,\"quality\":%u,\"values\":[",id,(unsigned long)g_device.boot,(unsigned long)now,s.linkUp?"true":"false",fresh,s.alarmFlags,s.linkQuality);
        for(uint8_t i=0;i<obd::VF_COUNT;++i){if(fresh&(1u<<i))pos+=snprintf(json+pos,sizeof(json)-pos,"%s%.3f",i?",":"",values[i]);else pos+=snprintf(json+pos,sizeof(json)-pos,"%snull",i?",":"");}
        pos+=snprintf(json+pos,sizeof(json)-pos,"],\"candidate_values\":[");
        for(uint8_t i=0;i<4;++i){if(obd::candidateFresh(s,i,now))pos+=snprintf(json+pos,sizeof(json)-pos,"%s%.3f",i?",":"",s.candidateValues[i]);else pos+=snprintf(json+pos,sizeof(json)-pos,"%snull",i?",":"");}
        pos+=snprintf(json+pos,sizeof(json)-pos,"],\"candidate_states\":[%u,%u,%u,%u],\"candidate_age_ms\":[%lu,%lu,%lu,%lu]}",s.candidateStates[0],s.candidateStates[1],s.candidateStates[2],s.candidateStates[3],(unsigned long)(now-s.candidateMs[0]),(unsigned long)(now-s.candidateMs[1]),(unsigned long)(now-s.candidateMs[2]),(unsigned long)(now-s.candidateMs[3]));
        Serial.println(json);return;
    }
#endif
    if(!strcmp(op,"GET")) {usbConfig(id);return;}
    if(!strcmp(op,"EVENTS")) {usbEvents(id);return;}
    if(!strcmp(op,"STATUS")) {
#if OBD_DRIVE_REPLAY
        const char* build="drive-replay-v4";
#elif OBD_DATA_TEST
        const char* build=OBD_AUTO_ADAPT?"data-lab-open-v33":"data-lab-probe-v29";
#else
        const char* build=OBD_AUTO_ADAPT?"obd-open-v33":"lancer-probe-v29";
#endif
        bool heroCached=false;
#if OBD_DISPLAY_TFT
        heroCached=g_ui.heroCached();
#endif
        const char* transport=OBD_DRIVE_REPLAY?"REPLAY":kTransportName;
#if OBD_MULTI_ADAPTER
        transport=obd::adapterModeName(g_tp.mode());
#endif
        Serial.printf("USB|{\"id\":%u,\"type\":\"status\",\"ok\":true,\"profile\":\"%s\",\"build\":\"%s %s %s\",\"transport\":\"%s\",\"bt\":\"%s\",\"elm\":%u,\"ready\":%s,\"uptime_ms\":%lu,\"boot_id\":%lu,\"reset_reason\":%d,\"heap\":%u,\"min_heap\":%u,\"internal_heap\":%u,\"largest_internal_block\":%u,\"hero_cache\":%s,\"ui_frames\":%lu,\"valid_mask\":%u,\"quality\":%u,\"reinits\":%lu,\"reconnects\":%lu,\"gear_calibrated\":false,\"storage_ok\":%s,\"event_pending\":%s,\"age_ms\":[%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu]}\n",
            id,OBD_AUTO_ADAPT?"STANDARD-OBD-AUTO":"LANCER-4B10-2011-MT",build,__DATE__,__TIME__,transport,g_tp.describeState().c_str(),unsigned(g_elm.state()),s.linkUp?"true":"false",(unsigned long)now,(unsigned long)g_device.boot,int(esp_reset_reason()),ESP.getFreeHeap(),ESP.getMinFreeHeap(),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),heroCached?"true":"false",(unsigned long)g_uiFrames,s.validMask,s.linkQuality,(unsigned long)s.reinits,(unsigned long)s.reconnects,g_device.available && !g_device.writeFailed?"true":"false",g_device.events.dirty?"true":"false",
            (unsigned long)(now-s.valueMs[0]),(unsigned long)(now-s.valueMs[1]),(unsigned long)(now-s.valueMs[2]),(unsigned long)(now-s.valueMs[3]),(unsigned long)(now-s.valueMs[4]),(unsigned long)(now-s.valueMs[5]),(unsigned long)(now-s.valueMs[6]),(unsigned long)(now-s.valueMs[7]));return;
    }
    if(!strncmp(op,"SET ",4) || !strcmp(op,"DEFAULT") || !strcmp(op,"MUTE_TOGGLE") || !strcmp(op,"SOUND_NEXT")) {
        obd::DeviceSettings next;
        if(!strcmp(op,"MUTE_TOGGLE")) {next=g_device.settings;next.muted^=1;}
        else if(!strcmp(op,"SOUND_NEXT")) {next=g_device.settings;next.soundLevel=(next.soundLevel+1)%4;}
        else if(strcmp(op,"DEFAULT") && !obd::parseSettings(op+4,next)){usbReply(id,"config",false,"invalid_settings");return;}
        if(!g_device.save(next)){usbReply(id,"config",false,"save_failed");return;}
        for(int i=0;i<2;++i)g_device.events.finish(i,now,obd::EventEnd::SettingsChanged);
        memset(g_alarmState,0,sizeof(g_alarmState));s.alarmFlags=0;s.settings=next;
        usbConfig(id);return;
    }
    if(!strcmp(op,"VIEW GET")) {
#if OBD_DISPLAY_TFT
        usbView(id,"current");
#else
        usbReply(id,"page",false,"tft_required");
#endif
        return;
    }
    obd::DisplayPage requested=obd::DisplayPage(g_requestedPage.load());
    if(obd::parsePageRequest(op,requested,requested)) {
#if OBD_DISPLAY_TFT
        g_requestedPage.store(uint8_t(requested));usbView(id,"requested");
#else
        usbReply(id,"page",false,"tft_required");
#endif
        return;
    }
    if(!strncmp(op,"VIEW ",5) || !strncmp(op,"PAGE ",5)) {usbReply(id,"page",false,"invalid_page");return;}
    if(!strcmp(op,"PATTERN")) {
#if OBD_DISPLAY_TFT
        g_tftTestRequested.store(true);usbReply(id,"pattern",true,"queued_visual_check_required");
#else
        usbReply(id,"pattern",false,"tft_required");
#endif
        return;
    }
    if(!strcmp(op,"RECONNECT") || !strcmp(op,"FORGET")) {
#if OBD_TRANSPORT == OBD_TP_BT
#if defined(OBD_BT_PC_SIM) && OBD_BT_PC_SIM
        if(!strcmp(op,"FORGET")){usbReply(id,"link",false,"bench_target_fixed");return;}
#else
        if(!strcmp(op,"FORGET"))g_tp.forgetMac();
#endif
        g_tp.requestReconnect(now);++s.reconnects;
#endif
        g_scanOn=g_scanPending=false;g_elm.requestReinit();s.linkUp=false;s.validMask=0;
        usbReply(id,"link",true,"requested");return;
    }
    usbReply(id,"error",false,"unknown_command");
}
