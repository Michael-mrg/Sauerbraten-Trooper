#include "game.h"

namespace game
{
    bool intermission = false;
    int maptime = 0, maprealtime = 0, minremain = 0, maptimeleft = 0;
    int respawnent = -1;
    int lasthit = 0, lastspawnattempt = 0;

    int following = -1, followdir = 0;

    fpsent *player1 = NULL;         // our client
    vector<fpsent *> players;       // other clients
    int savedammo[NUMGUNS];

    bool clientoption(const char *arg) { return false; }

    void taunt()
    {
        if(player1->state!=CS_ALIVE || player1->physstate<PHYS_SLOPE) return;
        if(lastmillis-player1->lasttaunt<1000) return;
        player1->lasttaunt = lastmillis;
        addmsg(SV_TAUNT, "rc", player1);
    }
    COMMAND(taunt, "");

	void follow(char *arg)
    {
        if(arg[0] ? player1->state==CS_SPECTATOR : following>=0)
        {
            following = arg[0] ? parseplayer(arg) : -1;
            if(following==player1->clientnum) following = -1;
            followdir = 0;
            conoutf("follow %s", following>=0 ? "on" : "off");
        }
	}
    COMMAND(follow, "s");

    void nextfollow(int dir)
    {
        if(player1->state!=CS_SPECTATOR || clients.empty())
        {
            stopfollowing();
            return;
        }
        int cur = following >= 0 ? following : (dir < 0 ? clients.length() - 1 : 0);
        loopv(clients)
        {
            cur = (cur + dir + clients.length()) % clients.length();
            if(clients[cur] && clients[cur]->state!=CS_SPECTATOR)
            {
                if(following<0) conoutf("follow on");
                following = cur;
                followdir = dir;
                return;
            }
        }
        stopfollowing();
    }
    ICOMMAND(nextfollow, "i", (int *dir), nextfollow(*dir < 0 ? -1 : 1));


    const char *getclientmap() { return clientmap; }

    void resetgamestate()
    {
        if(m_classicsp)
        {
            clearmovables();
            clearmonsters();                 // all monsters back at their spawns for editing
            resettriggers();
        }
        clearprojectiles();
        clearbouncers();
    }

    fpsent *spawnstate(fpsent *d)              // reset player state not persistent accross spawns
    {
        d->respawn();
        d->spawnstate(gamemode);
        return d;
    }

    void respawnself()
    {
        if(paused || ispaused()) return;
        if(m_mp(gamemode))
        {
            if(player1->respawned!=player1->lifesequence)
            {
                addmsg(SV_TRYSPAWN, "rc", player1);
                player1->respawned = player1->lifesequence;
            }
        }
        else
        {
            spawnplayer(player1);
            showscores(false);
            lasthit = 0;
            if(cmode) cmode->respawned(player1);
        }
    }

    fpsent *pointatplayer()
    {
        loopv(players) if(players[i] != player1 && intersect(players[i], player1->o, worldpos)) return players[i];
        return NULL;
    }

    void stopfollowing()
    {
        if(following<0) return;
        following = -1;
        followdir = 0;
        conoutf("follow off");
    }

    fpsent *followingplayer()
    {
        if(player1->state!=CS_SPECTATOR || following<0) return NULL;
        fpsent *target = getclient(following);
        if(target && target->state!=CS_SPECTATOR) return target;
        return NULL;
    }

    fpsent *hudplayer()
    {
        if(thirdperson) return player1;
        fpsent *target = followingplayer();
        return target ? target : player1;
    }

    void setupcamera()
    {
        fpsent *target = followingplayer();
        if(target)
        {
            player1->yaw = target->yaw;
            player1->pitch = target->state==CS_DEAD ? 0 : target->pitch;
            player1->o = target->o;
            player1->resetinterp();
        }
    }

    bool detachcamera()
    {
        fpsent *d = hudplayer();
        return d->state==CS_DEAD;
    }

    bool collidecamera()
    {
        switch(player1->state)
        {
            case CS_EDITING: return false;
            case CS_SPECTATOR: return followingplayer()!=NULL;
        }
        return true;
    }

    VARP(smoothmove, 0, 75, 100);
    VARP(smoothdist, 0, 32, 64);

    void predictplayer(fpsent *d, bool move)
    {
        d->o = d->newpos;
        d->yaw = d->newyaw;
        d->pitch = d->newpitch;
        if(move)
        {
            moveplayer(d, 1, false);
            d->newpos = d->o;
        }
        float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
        if(k>0)
        {
            d->o.add(vec(d->deltapos).mul(k));
            d->yaw += d->deltayaw*k;
            if(d->yaw<0) d->yaw += 360;
            else if(d->yaw>=360) d->yaw -= 360;
            d->pitch += d->deltapitch*k;
        }
    }

    void otherplayers(int curtime)
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || d->ai) continue;

            if(d->state==CS_ALIVE)
            {
                if(lastmillis - d->lastaction >= d->gunwait) d->gunwait = 0;
                if(d->quadmillis) entities::checkquad(curtime, d);
            }
            else if(d->state==CS_DEAD && d->ragdoll) moveragdoll(d);

            const int lagtime = lastmillis-d->lastupdate;
            if(!lagtime || intermission) continue;
            else if(lagtime>1000 && d->state==CS_ALIVE)
            {
                d->state = CS_LAGGED;
                continue;
            }
            if(d->state==CS_ALIVE || d->state==CS_EDITING)
            {
                if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
                else moveplayer(d, 1, false);
            }
            else if(d->state==CS_DEAD && !d->ragdoll && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
        }
    }

    VARFP(slowmosp, 0, 0, 1,
    {
        if(m_sp && !slowmosp) setvar("gamespeed", 100);
    });

    void checkslowmo()
    {
        static int lastslowmohealth = 0;
        setvar("gamespeed", intermission ? 100 : clamp(player1->health, 25, 200), true, false);
        if(player1->health<player1->maxhealth && lastmillis-max(maptime, lastslowmohealth)>player1->health*player1->health/2)
        {
            lastslowmohealth = lastmillis;
            player1->health++;
        }
    }

    void updateworld()        // main game update loop
    {
        if(!maptime) { maptime = lastmillis; maprealtime = totalmillis; return; }
        if(!curtime) { gets2c(); if(player1->clientnum>=0) c2sinfo(); return; }

        physicsframe();
        ai::trydropwaypoints();
        entities::checkquad(curtime, player1);
        updateweapons(curtime);
        otherplayers(curtime);
        ai::update();
        moveragdolls();
        gets2c();
        updatemovables(curtime);
        updatemonsters(curtime);
        if(player1->state==CS_DEAD)
        {
            if(player1->ragdoll) moveragdoll(player1);
            else if(lastmillis-player1->lastpain<2000)
            {
                player1->move = player1->strafe = 0;
                moveplayer(player1, 10, false);
            }
        }
        else if(!intermission)
        {
            if(player1->ragdoll) cleanragdoll(player1);
            moveplayer(player1, 10, true);
            swayhudgun(curtime);
            entities::checkitems(player1);
            if(m_sp)
            {
                if(slowmosp) checkslowmo();
                if(m_classicsp) checktriggers();
            }
            else if(cmode) cmode->checkitems(player1);
        }
        if(player1->clientnum>=0) c2sinfo();   // do this last, to reduce the effective frame lag
    }

    void spawnplayer(fpsent *d)   // place at random spawn
    {
        if(cmode) cmode->pickspawn(d);
        else findplayerspawn(d, d==player1 && respawnent>=0 ? respawnent : -1);
        spawnstate(d);
        if(d==player1)
        {
            if(editmode) d->state = CS_EDITING;
            else if(d->state != CS_SPECTATOR) d->state = CS_ALIVE;
        }
        else d->state = CS_ALIVE;
    }

    VARP(spawnwait, 0, 0, 1000);

    void respawn()
    {
        if(player1->state==CS_DEAD)
        {
            player1->attacking = false;
            int wait = cmode ? cmode->respawnwait(player1) : 0;
            if(wait>0)
            {
                lastspawnattempt = lastmillis;
                //conoutf(CON_GAMEINFO, "\f2you must wait %d second%s before respawn!", wait, wait!=1 ? "s" : "");
                return;
            }
            if(lastmillis < player1->lastpain + spawnwait) return;
            if(m_dmsp) { changemap(clientmap, gamemode); return; }    // if we die in SP we try the same map again
            respawnself();
            if(m_classicsp)
            {
                conoutf(CON_GAMEINFO, "\f2You wasted another life! The monsters stole your armour and some ammo...");
                loopi(NUMGUNS) if(i!=GUN_PISTOL && (player1->ammo[i] = savedammo[i]) > 5) player1->ammo[i] = max(player1->ammo[i]/3, 5);
            }
        }
    }

    // inputs

    void doattack(bool on)
    {
        if(intermission) return;
        if((player1->attacking = on)) respawn();
    }

    bool canjump()
    {
        if(!intermission) respawn();
        return player1->state!=CS_DEAD && !intermission;
    }

    bool allowmove(physent *d)
    {
        if(d->type!=ENT_PLAYER) return true;
        return !((fpsent *)d)->lasttaunt || lastmillis-((fpsent *)d)->lasttaunt>=1000;
    }

    VARP(hitsound, 0, 0, 1);

    void damaged(int damage, fpsent *d, fpsent *actor, bool local)
    {
        if(d->state!=CS_ALIVE || intermission) return;

        if(local) damage = d->dodamage(damage);
        else if(actor==player1) return;

        fpsent *h = hudplayer();
        if(h!=player1 && actor==h && d!=actor)
        {
            if(hitsound && lasthit != lastmillis) playsound(S_HIT);
            lasthit = lastmillis;
        }
        if(d==h)
        {
            damageblend(damage);
            damagecompass(damage, actor->o);
        }
        damageeffect(damage, d, d!=h);

		ai::damaged(d, actor);

        if(m_sp && slowmosp && d==player1 && d->health < 1) d->health = 1;

        if(d->health<=0) { if(local) killed(d, actor); }
        else if(d==h) playsound(S_PAIN6);
        else playsound(S_PAIN1+rnd(5), &d->o);
    }

    VARP(deathscore, 0, 1, 1);

    void deathstate(fpsent *d, bool restore)
    {
        d->state = CS_DEAD;
        d->lastpain = lastmillis;
        d->superdamage = restore ? 0 : max(-d->health, 0);
        if(d==player1)
        {
            if(deathscore) showscores(true);
            disablezoom();
            if(!restore) loopi(NUMGUNS) savedammo[i] = player1->ammo[i];
            d->attacking = false;
            if(!restore) d->deaths++;
            //d->pitch = 0;
            d->roll = 0;
            playsound(S_DIE1+rnd(2));
        }
        else
        {
            d->move = d->strafe = 0;
            d->deaths ++;
            d->resetinterp();
            d->smoothmillis = 0;
            playsound(S_DIE1+rnd(2), &d->o);
            if(!restore) superdamageeffect(d->vel, d);
        }
    }

    void killed(fpsent *d, fpsent *actor)
    {
        if(d->state==CS_EDITING)
        {
            d->editstate = CS_DEAD;
            if(d==player1) d->deaths++;
            else d->resetinterp();
            return;
        }
        else if(d->state!=CS_ALIVE || intermission) return;

        fpsent *h = followingplayer();
        if(!h) h = player1;
        int contype = d==h || actor==h ? CON_FRAG_SELF : CON_FRAG_OTHER;
        string dname, aname;
        copystring(dname, d==player1 ? "you" : colorname(d));
        copystring(aname, actor==player1 ? "you" : colorname(actor));
        if(actor->type==ENT_AI)
            conoutf(contype, "\f2%s got killed by %s!", dname, aname);
        else if(d==actor || actor->type==ENT_INANIMATE)
            conoutf(contype, "\f2%s suicided%s", dname, d==player1 ? "!" : "");
        else if(isteam(d->team, actor->team))
        {
            if(actor==player1) conoutf(contype, "\f3you fragged a teammate (%s)", dname);
            else if(d==player1) conoutf(contype, "\f3you got fragged by a teammate (%s)", aname);
            else conoutf(contype, "\f2%s fragged a teammate (%s)", aname, dname);
        }
        else
        {
            if(d==player1) conoutf(contype, "\f2you got fragged by %s", aname);
            else conoutf(contype, "\f2%s fragged %s", aname, dname);
        }
        deathstate(d);
		ai::killed(d, actor);
    }

    VARP(showtimehud, 0, 0, 1);
    void timeupdate(int timeremain)
    {
        minremain = timeremain;
        if(!timeremain)
        {
            intermission = true;
            player1->attacking = false;
            if(cmode) cmode->gameover();
            conoutf(CON_GAMEINFO, "\f2intermission:");
            conoutf(CON_GAMEINFO, "\f2game has ended!");
            conoutf(CON_GAMEINFO, "\f2player frags: %d, deaths: %d", player1->frags, player1->deaths);
            int accuracy = player1->totaldamage*100/max(player1->totalshots, 1);
            conoutf(CON_GAMEINFO, "\f2player total damage dealt: %d, damage wasted: %d, accuracy(%%): %d", player1->totaldamage, player1->totalshots-player1->totaldamage, accuracy);
            if(m_sp) spsummary(accuracy);

            showscores(true);
            disablezoom();
            
            if(identexists("intermission")) execute("intermission");
        }
        else if(timeremain > 0 && !showtimehud)
        {
            conoutf(CON_GAMEINFO, "\f2time remaining: %d %s", timeremain, timeremain==1 ? "minute" : "minutes");
        }
        maptimeleft = (lastmillis + timeremain * 60000) * (maptimeleft == -1 ? -1 : 1);
    }

    vector<fpsent *> clients;

    fpsent *newclient(int cn)   // ensure valid entity
    {
        if(cn < 0 || cn > max(0xFF, MAXCLIENTS + MAXBOTS))
        {
            neterr("clientnum", false);
            return NULL;
        }

        if(cn == player1->clientnum) return player1;

        while(cn >= clients.length()) clients.add(NULL);
        if(!clients[cn])
        {
            fpsent *d = new fpsent;
            d->clientnum = cn;
            clients[cn] = d;
            players.add(d);
        }
        return clients[cn];
    }

    fpsent *getclient(int cn)   // ensure valid entity
    {
        if(cn == player1->clientnum) return player1;
        return clients.inrange(cn) ? clients[cn] : NULL;
    }

    void clientdisconnected(int cn, bool notify)
    {
        if(!clients.inrange(cn)) return;
        if(following==cn)
        {
            if(followdir) nextfollow(followdir);
            else stopfollowing();
        }
        fpsent *d = clients[cn];
        if(!d) return;
        if(notify && d->name[0]) conoutf("player %s disconnected", colorname(d));
        removeweapons(d);
        removetrackedparticles(d);
        removetrackeddynlights(d);
        if(cmode) cmode->removeplayer(d);
        players.removeobj(d);
        DELETEP(clients[cn]);
        cleardynentcache();
    }

    void clearclients(bool notify)
    {
        loopv(clients) if(clients[i]) clientdisconnected(i, notify);
    }

    void initclient()
    {
        player1 = spawnstate(new fpsent);
        players.add(player1);
    }

    VARP(showmodeinfo, 0, 1, 1);

    void startgame()
    {
        clearmovables();
        clearmonsters();

        clearprojectiles();
        clearbouncers();
        clearragdolls();

        // reset perma-state
        loopv(players)
        {
            fpsent *d = players[i];
            d->frags = 0;
            d->deaths = 0;
            d->totaldamage = 0;
            d->totalshots = 0;
            d->maxhealth = 100;
            d->lifesequence = -1;
            d->respawned = d->suicided = -2;
        }

        setclientmode();

        intermission = false;
        maptime = 0;

        if(cmode)
        {
            cmode->preload();
            cmode->setup();
        }

        conoutf(CON_GAMEINFO, "\f2game mode is %s", server::modename(gamemode));

        if(m_sp)
        {
            defformatstring(scorename)("bestscore_%s", getclientmap());
            const char *best = getalias(scorename);
            if(*best) conoutf(CON_GAMEINFO, "\f2try to beat your best score so far: %s", best);
        }
        else
        {
            const char *info = m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
            if(showmodeinfo && info) conoutf(CON_GAMEINFO, "\f0%s", info);
        }

        if(player1->playermodel != playermodel) switchplayermodel(playermodel);

        showscores(false);
        disablezoom();
        lasthit = 0;

        if(identexists("mapstart")) execute("mapstart");
    }

    void startmap(const char *name)   // called just after a map load
    {
        ai::savewaypoints();
        ai::clearwaypoints(true);

        respawnent = -1; // so we don't respawn at an old spot
        if(!m_mp(gamemode)) spawnplayer(player1);
        else findplayerspawn(player1, -1);
        entities::resetspawns();
        copystring(clientmap, name ? name : "");

        sendmapinfo();
    }

    const char *getmapinfo()
    {
        return showmodeinfo && m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
    }

    void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material)
    {
        if(d->type==ENT_INANIMATE) return;
        if     (waterlevel>0) { if(material!=MAT_LAVA) playsound(S_SPLASH1, d==player1 ? NULL : &d->o); }
        else if(waterlevel<0) playsound(material==MAT_LAVA ? S_BURN : S_SPLASH2, d==player1 ? NULL : &d->o);
        if     (floorlevel>0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_JUMP, d); }
        else if(floorlevel<0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_LAND, d); }
    }

    void msgsound(int n, physent *d)
    {
        if(!d || d==player1)
        {
            addmsg(SV_SOUND, "ci", d, n);
            playsound(n);
        }
        else
        {
            if(d->type==ENT_PLAYER && ((fpsent *)d)->ai)
                addmsg(SV_SOUND, "ci", d, n);
            playsound(n, &d->o);
        }
    }

    int numdynents() { return players.length()+monsters.length()+movables.length(); }

    dynent *iterdynents(int i)
    {
        if(i<players.length()) return players[i];
        i -= players.length();
        if(i<monsters.length()) return (dynent *)monsters[i];
        i -= monsters.length();
        if(i<movables.length()) return (dynent *)movables[i];
        return NULL;
    }

    bool duplicatename(fpsent *d, const char *name = NULL)
    {
        if(!name) name = d->name;
        loopv(players) if(d!=players[i] && !strcmp(name, players[i]->name)) return true;
        return false;
    }
    
    VARP(colornames_offset, 0, 0, 1023);
    int create_colored_name(const char *name, char *t_clan, char *t_name)
    {
        int indices[4] = {0};
        int len = strlen(name);
        const char *startchars = "[|{}=<(/\\";
        const char *endchars = "]|{}=>):/\\.";
        
        while(strchr(startchars, name[indices[0]]))
            indices[0] ++;
        if(!indices[0]) // name|TAG|
        {
            int t = len - 1;
            while(t != -1 && strchr(endchars, name[t]))
                t --;
            if(t != len - 1)
            {
                indices[1] = t + 1;
                while(t != -1 && !strchr(startchars, name[t]))
                    t --;
                if(t < 1)
                    return -1;
                indices[0] = t + 1; indices[2] = 0; indices[3] = indices[0] - 1;
                while(strchr(startchars, name[indices[3]-1]))
                    indices[3] --;
            }
        }
        if(!indices[1]) // |TAG|name
        {
            indices[1] = indices[0];
            while(!strchr(endchars, name[indices[1]]))
                indices[1] ++;
            indices[2] = indices[1];
            while(strchr(endchars, name[indices[2]]))
                indices[2] ++;
            indices[3] = len;
        }
        if(indices[1] == len)
            indices[1] = indices[2] = 0;
        
        // If one of the tag separators is a bound, and clan_name > real_name, swap.
        if(indices[3] == len && !indices[0] && len < indices[1]+indices[2])
        {
            int a = indices[0], b = indices[1];
            indices[0] = indices[2], indices[1] = indices[3];
            indices[2] = a, indices[3] = b;
        }
        
        if(indices[0] < 0 || indices[1] < 0 || indices[2] < 0 || indices[3] < 0 ||
           indices[0] > len || indices[1] > len || indices[2] > len || indices[3] > len ||
           indices[1] <= indices[0] || indices[3] <= indices[2])
            return -1;
        memset(t_clan, '\0', 260);
        memset(t_name, '\0', 260);
        strncpy(t_clan, name+indices[0], indices[1]-indices[0]);
        strncpy(t_name, name+indices[2], indices[3]-indices[2]);
        
        int c = 0;
        for(int i = 1; i < strlen(t_clan); i ++)
            c += (t_clan[i] + colornames_offset) ^ t_clan[i-1];
        return 'A' + (c % 13);
    }
    
    VARP(colornames, 0, 0, 1);
    const char *colorname(fpsent *d, const char *name, const char *prefix)
    {
        if(!name) name = d->name;
        int h = (colornames << 10) | colornames_offset;
        if(d->name_cache_colored != h || strcmp(name, d->name_cache))
        {
            d->colored_name[0] = 0;
            strcpy(d->name_cache, name);
            d->name_cache_colored = h;
            if(colornames)
            {
                string t_name, t_clan;
                int color = create_colored_name(name, t_clan, t_name);
                if(color != -1)
                    formatstring(d->colored_name)("\fs\f%c%s\fr %s", color, t_clan, t_name);
            }
            if(!d->colored_name[0])
                strcpy(d->colored_name, name);
        }
        // In SV_SWITCHNAME, returning colored_name would fail because the name arg is different.
        // This code probably shouldn't cache names if name isn't NULL.
        static string cname[3];
        static int cidx = 0;
        cidx = (cidx + 1) % 3;
        strcpy(cname[cidx], d->colored_name);
        bool is_bot = d->aitype != AI_NONE;
        if(is_bot || duplicatename(d, name))
        {
            string t;
            formatstring(t)(is_bot ? " \fs\f5[%d]\fr" : " \fs\f5(%d)\fr", d->clientnum);
            strcat(cname[cidx], t);
        }
        return cname[cidx];
    }

    void suicide(physent *d)
    {
        if(d==player1 || (d->type==ENT_PLAYER && ((fpsent *)d)->ai))
        {
            if(d->state!=CS_ALIVE) return;
            fpsent *pl = (fpsent *)d;
            if(!m_mp(gamemode)) killed(pl, pl);
            else if(pl->suicided!=pl->lifesequence)
            {
                addmsg(SV_SUICIDE, "rc", pl);
                pl->suicided = pl->lifesequence;
            }
        }
        else if(d->type==ENT_AI) suicidemonster((monster *)d);
        else if(d->type==ENT_INANIMATE) suicidemovable((movable *)d);
    }
    ICOMMAND(kill, "", (), suicide(player1));

    void drawicon(int icon, float x, float y, float sz)
    {
        settexture("packages/hud/items.png");
        glBegin(GL_QUADS);
        float tsz = 0.25f, tx = tsz*(icon%4), ty = tsz*(icon/4);
        glTexCoord2f(tx,     ty);     glVertex2f(x,    y);
        glTexCoord2f(tx+tsz, ty);     glVertex2f(x+sz, y);
        glTexCoord2f(tx+tsz, ty+tsz); glVertex2f(x+sz, y+sz);
        glTexCoord2f(tx,     ty+tsz); glVertex2f(x,    y+sz);
        glEnd();
    }

    float abovegameplayhud()
    {
        switch(hudplayer()->state)
        {
            case CS_EDITING:
            case CS_SPECTATOR:
                return 1;
            default:
                return 1650.0f/1800.0f;
        }
    }

    int ammohudup[3] = { GUN_CG, GUN_RL, GUN_GL },
        ammohuddown[3] = { GUN_RIFLE, GUN_SG, GUN_PISTOL },
        ammohudcycle[7] = { -1, -1, -1, -1, -1, -1, -1 };

    ICOMMAND(ammohudup, "sss", (char *w1, char *w2, char *w3),
    {
        int i = 0;
        if(w1[0]) ammohudup[i++] = atoi(w1);
        if(w2[0]) ammohudup[i++] = atoi(w2);
        if(w3[0]) ammohudup[i++] = atoi(w3);
        while(i < 3) ammohudup[i++] = -1;
    });

    ICOMMAND(ammohuddown, "sss", (char *w1, char *w2, char *w3),
    {
        int i = 0;
        if(w1[0]) ammohuddown[i++] = atoi(w1);
        if(w2[0]) ammohuddown[i++] = atoi(w2);
        if(w3[0]) ammohuddown[i++] = atoi(w3);
        while(i < 3) ammohuddown[i++] = -1;
    });

    ICOMMAND(ammohudcycle, "sssssss", (char *w1, char *w2, char *w3, char *w4, char *w5, char *w6, char *w7),
    {
        int i = 0;
        if(w1[0]) ammohudcycle[i++] = atoi(w1);
        if(w2[0]) ammohudcycle[i++] = atoi(w2);
        if(w3[0]) ammohudcycle[i++] = atoi(w3);
        if(w4[0]) ammohudcycle[i++] = atoi(w4);
        if(w5[0]) ammohudcycle[i++] = atoi(w5);
        if(w6[0]) ammohudcycle[i++] = atoi(w6);
        if(w7[0]) ammohudcycle[i++] = atoi(w7);
        while(i < 7) ammohudcycle[i++] = -1;
    });

    VARP(ammohud, 0, 1, 1);

    void drawammohud(fpsent *d)
    {
        float x = HICON_X + 2*HICON_STEP, y = HICON_Y, sz = HICON_SIZE;
        glPushMatrix();
        glScalef(1/3.2f, 1/3.2f, 1);
        float xup = (x+sz)*3.2f, yup = y*3.2f + 0.1f*sz;
        loopi(3)
        {
            int gun = ammohudup[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            drawicon(HICON_FIST+gun, xup, yup, sz);
            yup += sz;
        }
        float xdown = x*3.2f - sz, ydown = (y+sz)*3.2f - 0.1f*sz;
        loopi(3)
        {
            int gun = ammohuddown[3-i-1];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            ydown -= sz;
            drawicon(HICON_FIST+gun, xdown, ydown, sz);
        }
        int offset = 0, num = 0;
        loopi(7)
        {
            int gun = ammohudcycle[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL) continue;
            if(gun == d->gunselect) offset = i + 1;
            else if(d->ammo[gun]) num++;
        }
        float xcycle = (x+sz/2)*3.2f + 0.5f*num*sz, ycycle = y*3.2f-sz;
        loopi(7)
        {
            int gun = ammohudcycle[(i + offset)%7];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            xcycle -= sz;
            drawicon(HICON_FIST+gun, xcycle, ycycle, sz);
        }
        glPopMatrix();
    }

    void drawhudicons(fpsent *d)
    {
        glPushMatrix();
        glScalef(2, 2, 1);

        draw_textf("%d", (HICON_X + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->state==CS_DEAD ? 0 : d->health);
        if(d->state!=CS_DEAD)
        {
            if(d->armour) draw_textf("%d", (HICON_X + HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->armour);
            draw_textf("%d", (HICON_X + 2*HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, d->ammo[d->gunselect]);
        }

        glPopMatrix();

        drawicon(HICON_HEALTH, HICON_X, HICON_Y);
        if(d->state!=CS_DEAD)
        {
            if(d->armour) drawicon(HICON_BLUE_ARMOUR+d->armourtype, HICON_X + HICON_STEP, HICON_Y);
            drawicon(HICON_FIST+d->gunselect, HICON_X + 2*HICON_STEP, HICON_Y);
            if(d->quadmillis) drawicon(HICON_QUAD, HICON_X + 3*HICON_STEP, HICON_Y);
            if(ammohud) drawammohud(d);
        }
    }

    void gameplayhud(int w, int h)
    {
        glPushMatrix();
        glScalef(h/1800.0f, h/1800.0f, 1);

        if(player1->state==CS_SPECTATOR)
        {
            int pw, ph, tw, th, fw, fh;
            text_bounds("  ", pw, ph);
            text_bounds("SPECTATOR", tw, th);
            th = max(th, ph);
            fpsent *f = followingplayer();
            text_bounds(f ? colorname(f) : " ", fw, fh);
            fh = max(fh, ph);
            draw_text("SPECTATOR", w*1800/h - tw - pw, 1650 - th - fh);
            if(f) draw_text(colorname(f), w*1800/h - fw - pw, 1650 - fh);
        }

        fpsent *d = hudplayer();
        if(d->state!=CS_EDITING)
        {
            if(d->state!=CS_SPECTATOR) drawhudicons(d);
            if(cmode) cmode->drawhud(d, w, h);
        }

        glPopMatrix();
    }

    int clipconsole(int w, int h)
    {
        if(cmode) return cmode->clipconsole(w, h);
        return 0;
    }

    VARP(teamcrosshair, 0, 1, 1);
    VARP(hitcrosshair, 0, 425, 1000);

    const char *defaultcrosshair(int index)
    {
        switch(index)
        {
            case 2: return "data/hit.png";
            case 1: return "data/teammate.png";
            default: return "data/crosshair.png";
        }
    }

    VARP(crosshairhealth, 0, 1, 1);
    int selectcrosshair(float &r, float &g, float &b)
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_DEAD) return -1;

        if(d->state!=CS_ALIVE) return 0;

        int crosshair = 0;
        if(lasthit && lastmillis - lasthit < hitcrosshair) crosshair = 2;
        else if(teamcrosshair)
        {
            dynent *o = intersectclosest(d->o, worldpos, d);
            if(o && o->type==ENT_PLAYER && isteam(((fpsent *)o)->team, d->team))
            {
                crosshair = 1;
                r = g = 0;
            }
        }

        if(crosshair!=1 && !editmode && !m_insta && crosshairhealth)
        {
            if(d->health<=25) { r = 1.0f; g = b = 0; }
            else if(d->health<=50) { r = 1.0f; g = 0.5f; b = 0; }
        }
        if(d->gunwait) { r *= 0.5f; g *= 0.5f; b *= 0.5f; }
        return crosshair;
    }

    void lighteffects(dynent *e, vec &color, vec &dir)
    {
#if 0
        fpsent *d = (fpsent *)e;
        if(d->state!=CS_DEAD && d->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif
    }

    bool serverinfostartcolumn(g3d_gui *g, int i)
    {
        static const char *names[] = { "ping ", "players ", "map ", "mode ", "master ", "host ", "port ", "description " };
        static const int struts[] =  { 0,       0,          12,     12,      8,         13,      6,       24 };
        if(size_t(i) >= sizeof(names)/sizeof(names[0])) return false;
        g->pushlist();
        g->text(names[i], 0xFFFF80, !i ? "server" : NULL);
        if(struts[i]) g->strut(struts[i]);
        g->mergehits(true);
        return true;
    }

    void serverinfoendcolumn(g3d_gui *g, int i)
    {
        g->mergehits(false);
        g->poplist();
    }

    VARP(colorserverbrowser, 0, 0, 1);
    bool serverinfoentry(g3d_gui *g, int i, const char *name, int port, const char *sdesc, const char *map, int ping, const vector<int> &attr, int np)
    {
        if(ping < 0 || attr.empty() || attr[0]!=PROTOCOL_VERSION)
        {
            switch(i)
            {
                case 0:
                    if(g->button(" ", 0xFFFFDD, "server")&G3D_UP) return true;
                    break;

                case 1:
                case 2:
                case 3:
                case 4:
                    if(g->button(" ", 0xFFFFDD)&G3D_UP) return true;
                    break;

                case 5:
                    if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                    break;

                case 6:
                    if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                    break;

                case 7:
                    if(ping < 0)
                    {
                        if(g->button(sdesc, 0xFFFFDD)&G3D_UP) return true;
                    }
                    else if(g->buttonf("[%s protocol] ", 0xFFFFDD, NULL, attr.empty() ? "unknown" : (attr[0] < PROTOCOL_VERSION ? "older" : "newer"))&G3D_UP) return true;
                    break;
            }
            return false;
        }

        int color = 0xFFFFDD;
        if(colorserverbrowser)
        {
            if(attr[1] == 1) color = 0xA68064; // coop edit
            else if(attr[4] > 1) color = 0x90EE90; // Mastermode locked or private
            else if(attr[3] == np) color = 0xFF4444; // Server full
            else if((float)np/attr[3] > 0.5) color = 0x8888FF; // Server >50% full
        }
        switch(i)
        {
            case 0:
                if(g->buttonf("%d ", color, "server", ping)&G3D_UP) return true;
                break;

            case 1:
                if(attr.length()>=4)
                {
                    if(g->buttonf("%d/%d ", color, NULL, np, attr[3])&G3D_UP) return true;
                }
                else if(g->buttonf("%d ", color, NULL, np)&G3D_UP) return true;
                break;

            case 2:
                if(g->buttonf("%.25s ", color, NULL, map)&G3D_UP) return true;
                break;

            case 3:
                if(g->buttonf("%s ", color, NULL, attr.length()>=2 ? server::modename(attr[1], "") : "")&G3D_UP) return true;
                break;

            case 4:
                if(g->buttonf("%s ", color, NULL, attr.length()>=5 ? server::mastermodename(attr[4], "") : "")&G3D_UP) return true;
                break;

            case 5:
                if(g->buttonf("%s ", color, NULL, name)&G3D_UP) return true;
                break;

            case 6:
                if(g->buttonf("%d ", color, NULL, port)&G3D_UP) return true;
                break;

            case 7:
                if(g->buttonf("%.25s", color, NULL, sdesc)&G3D_UP) return true;
                break;
        }
        return false;
    }

    // any data written into this vector will get saved with the map data. Must take care to do own versioning, and endianess if applicable. Will not get called when loading maps from other games, so provide defaults.
    void writegamedata(vector<char> &extras) {}
    void readgamedata(vector<char> &extras) {}

    const char *gameident() { return "fps"; }
    const char *savedconfig() { return "config.cfg"; }
    const char *defaultconfig() { return "data/defaults.cfg"; }
    const char *autoexec() { return "autoexec.cfg"; }
    const char *savedservers() { return "servers.cfg"; }

    void loadconfigs()
    {
        execfile("auth.cfg", false);
    }
    
    VARP(showpinghud, 0, 0, 1);
    void renderpinghud(int w, int h, int fonth)
    {
        if(showpinghud && !m_sp && hudplayer()->state != CS_EDITING) {
            if(!showpj) {
                draw_textf("%d", w*3 - 9 * fonth, h*3 - fonth*5/2, player1->ping);
                return;
            }
            int pj = 0, c = 0;
            loopv(players)
                if(players[i]->plag != 0 && players[i]->state != CS_SPECTATOR) {
                    pj += players[i]->plag;
                    c ++;
                }
            if(showping)
                draw_textf("%d,%d", w*3 - 9 * fonth, h*3 - fonth*5/2, player1->ping, c ? pj / c : 0);
            else
                draw_textf("%d", w*3 - 9 * fonth, h*3 - fonth*5/2, c ? pj / c : 0);
        }
    }
    

    // Credit to WahnFred
    void rendertimehud(int w, int h, int fonth)
    {
        if(showtimehud && !m_sp && !m_edit && hudplayer()->state != CS_EDITING)
        {
            int timeleft = abs(maptimeleft);
            int t = intermission ? 0 : (timeleft + 1000 - lastmillis) / 1000;
            int m = t / 60, s = t % 60;
            if(!m && s && s < 30 && lastmillis % 1000 >= 500) return;
                // (!m_edit && !m) ? "\f3" : (!s) ? "\f1" : "", m, s
            const char *color = m ? (s ? "" : "\f1") : "\f3";
            if(maptimeleft > -1)
                draw_textf("%s%d:%-2.2d", w*3 - 9*fonth, h*3 - fonth*3/2, color, m, s);
            else
                draw_textf("%s<%d min", w*3 - 9*fonth, h*3 - fonth*3/2, color, m+1);
        }
    }
    
    VARP(showscorehud, 0, 0, 1);
    void renderscorehud(int w, int h, int fonth)
    {
        if(showscorehud && !m_sp && !m_edit && hudplayer()->state != CS_EDITING)
        {
            vector<int> v;
            int n = getscores(v);
            if(!v.length()) return;
            string s;
            int f = 0;
            loopv(v)
                f += sprintf(s+f, "%s\fs%s%d\fr", f ? "-" : "", n == i ? "\f1" : "", v[i]);      
            draw_text(s, w*3 - 5*fonth, h*3 - fonth*5/2);
        }
    }

    VARP(showhighlights, 0, 0, 1);
    void renderhighlighthud(int w, int h, int fonth)
    {
        if(showhighlights && !m_sp && !m_edit && hudplayer()->state != CS_EDITING)
        {
            int k = 0, z = 0;
            loopv(players) {
                if(players[i]->state != CS_SPECTATOR && !isteam(players[i]->team, player1->team) && (players[i]->highlight || highlightall)) {
                    int rb = w*3-9*fonth-6*fonth*z;
                    const char *s = colorname(players[i]);
                    int sw, sh;
                    text_bounds(s, sw, sh);
                    draw_textf("%s%s", rb-sw, fonth*(0.66+k), players[i]->state == CS_DEAD ? "\f4" : "", s);
                    if(k ++ == 4)
                    {
                        k = 0;
                        z ++;
                    }
                }
            }
        }

    }
}

