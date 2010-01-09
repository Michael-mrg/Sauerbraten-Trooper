// creation of scoreboard
#include "game.h"

size_t strlcpy(char *d, const char *s, size_t size)
{
    size_t len = strlen(s);
    len = (len >= size) ? size - 1 : len;
    memcpy(d, s, len);
    d[len] = '\0';
    return len;
}

namespace game
{
    VARP(scoreboard2d, 0, 1, 1);
    VARP(showclientnum, 0, 0, 1);
    VARP(showpj, 0, 0, 1);
    VARP(showping, 0, 1, 1);
    VARP(showspectators, 0, 1, 1);
    VARP(highlightscore, 0, 1, 1);
    VARP(showconnecting, 0, 0, 1);
    VARP(colorclans, 0, 1, 1);
    VARP(showfrags, 0, 1, 1);

    int createname(const char *name, char *gclan, char *gname) {
        // Automatic colors go here
        int colors[] = {0xFFD300, 0x787EB7, 0x759536, 0x6B8775, 0xF5F5B3, 0xFB000D, 0x61D7A4, 0x532881, 0xF08BCB, 0xDC9630, 0xFEFCFF, 0x99A1DE};
        int colorsCount = 12;
        // Custom colors go here
        int customColors[] =  { 0x679FD2 };
        const char *customNames[] = { "mVa", 0 };

        if(strchr(name, ' ') != NULL) // Fail on bot [128]
            return -1;

        int clannamestart = 0;
        int clannameend = 0;
        int clantagstart = 0;
        int clantagend = 0;
        int realnamestart = 0;
        int realnameend = 0;
        int len = strlen(name);
        char clanname[250], realname[250];
        const char *startchars = "[|{}=<(/\\.";
        const char *endchars = "]|{}=>):/\\.";

        while(strchr(startchars, name[clannamestart]) != NULL) // Move clannamestart up
            clannamestart ++;
        if(clannamestart == 0) {
            int p = len-1;
            while(strchr(endchars, name[p]) != NULL) // Check for name|tag|
                p --;
            if(p != len-1) { // name|tag| found
                clannameend = len;
                clannamestart = p;
                while(strchr(startchars, name[clannamestart]) == NULL) // Move clannamestart back
                    clannamestart --;
                realnamestart = 0;
                realnameend = clannamestart;
                clannamestart ++;
                while(strchr(startchars, name[realnameend]) != NULL)
                    realnameend --;
                realnameend ++;
            }
        }
        if(clannameend == 0) { // name|tag| not found, look for |tag|name
            clannameend = clannamestart;
            while(strchr(endchars, name[clannameend]) == NULL)
                clannameend ++;
            clantagend = clannameend;
            while(strchr(endchars, name[clantagend]) != NULL)
                clantagend ++;
            realnamestart = clantagend;
            realnameend = len;
        }
        if(clannameend == len) {
            clannameend = 0;
            realnamestart = 0;
        }
        if(realnameend == len && clannamestart == 0 && len < clannameend+realnamestart) {
            int a = clannameend, b = clannamestart;
            clannameend = realnameend;
            clannamestart = realnamestart;
            realnameend = a;
            realnamestart = b;
        }
        if(realnamestart > strlen(name)) // Todo: Why does this happen. <name>
            return -1;
        strlcpy(clanname, name+clannamestart, clannameend-clannamestart+1);
        strlcpy(realname, name+realnamestart, realnameend+1);
        if(!strlen(clanname) || !strlen(realname))
            return -1;

        // Pick a color
        int color = 0xFFFFFF;
        int i = 0;
        while(customNames[i] && strcmp(customNames[i], clanname))
            i ++;
        if(customNames[i])
            color = customColors[i];
        else {
            int c = 0;
            for(int i = 1; i < strlen(clanname); i ++)
                c += clanname[i] ^ clanname[i-1];
            color = colors[c % colorsCount];
        }
        strcpy(gclan, clanname);
        strcpy(gname, realname);
        return color;
    }

    static int playersort(const fpsent **a, const fpsent **b)
    {
        if((*a)->state==CS_SPECTATOR)
        {
            if((*b)->state==CS_SPECTATOR) return strcmp((*a)->name, (*b)->name);
            else return 1;
        }
        else if((*b)->state==CS_SPECTATOR) return -1;
        if((*a)->frags > (*b)->frags) return -1;
        if((*a)->frags < (*b)->frags) return 1;
        return strcmp((*a)->name, (*b)->name);
    }

    void getbestplayers(vector<fpsent *> &best)
    {
        loopv(players)
        {
            fpsent *o = players[i];
            if(o->state!=CS_SPECTATOR) best.add(o);
        }
        best.sort(playersort);
        while(best.length()>1 && best.last()->frags < best[0]->frags) best.drop();
    }

    void sortteams(vector<teamscore> &teamscores)
    {
        if(cmode && cmode->hidefrags()) cmode->getteamscores(teamscores);

        loopv(players)
        {
            fpsent *o = players[i];
            teamscore *ts = NULL;
            loopv(teamscores) if(!strcmp(teamscores[i].team, o->team)) { ts = &teamscores[i]; break; }
            if(!ts) teamscores.add(teamscore(o->team, cmode && cmode->hidefrags() ? 0 : o->frags));
            else if(!cmode || !cmode->hidefrags()) ts->score += o->frags;
        }
        teamscores.sort(teamscore::compare);
    }

    void getbestteams(vector<const char *> &best)
    {
        vector<teamscore> teamscores;
        sortteams(teamscores);
        while(teamscores.length()>1 && teamscores.last().score < teamscores[0].score) teamscores.drop();
        loopv(teamscores) best.add(teamscores[i].team);
    }

    struct scoregroup : teamscore
    {
        vector<fpsent *> players;
    };
    static vector<scoregroup *> groups;
    static vector<fpsent *> spectators;

    static int scoregroupcmp(const scoregroup **x, const scoregroup **y)
    {
        if(!(*x)->team)
        {
            if((*y)->team) return 1;
        }
        else if(!(*y)->team) return -1;
        if((*x)->score > (*y)->score) return -1;
        if((*x)->score < (*y)->score) return 1;
        if((*x)->players.length() > (*y)->players.length()) return -1;
        if((*x)->players.length() < (*y)->players.length()) return 1;
        return (*x)->team && (*y)->team ? strcmp((*x)->team, (*y)->team) : 0;
    }

    static int groupplayers()
    {
        int numgroups = 0;
        spectators.setsize(0);
        loopv(players)
        {
            fpsent *o = players[i];
            if(!showconnecting && !o->name[0]) continue;
            if(o->state==CS_SPECTATOR) { spectators.add(o); continue; }
            const char *team = m_teammode && o->team[0] ? o->team : NULL;
            bool found = false;
            loopj(numgroups)
            {
                scoregroup &g = *groups[j];
                if(team!=g.team && (!team || !g.team || strcmp(team, g.team))) continue;
                if(team && (!cmode || !cmode->hidefrags())) g.score += o->frags;
                g.players.add(o);
                found = true;
            }
            if(found) continue;
            if(numgroups>=groups.length()) groups.add(new scoregroup);
            scoregroup &g = *groups[numgroups++];
            g.team = team;
            if(!team) g.score = 0;
            else if(cmode && cmode->hidefrags()) g.score = cmode->getteamscore(o->team);
            else g.score = o->frags;
            g.players.setsize(0);
            g.players.add(o);
        }
        loopi(numgroups) groups[i]->players.sort(playersort);
        spectators.sort(playersort);
        groups.sort(scoregroupcmp, 0, numgroups);
        return numgroups;
    }

    void renderscoreboard(g3d_gui &g, bool firstpass)
    {
        const char *mname = getclientmap();
        defformatstring(modemapstr)("%s: %s", server::modename(gamemode), mname[0] ? mname : "[new map]");
        if(m_timed && mname[0] && minremain >= 0)
        {
            if(!minremain) concatstring(modemapstr, ", intermission");
            else
            {
                defformatstring(timestr)(", %d %s remaining", minremain, minremain==1 ? "minute" : "minutes");
                concatstring(modemapstr, timestr);
            }
        }
        if(paused || ispaused()) concatstring(modemapstr, ", paused");

        g.text(modemapstr, 0xFFFF80, "server");
    
        int numgroups = groupplayers();
        loopk(numgroups)
        {
            if((k%2)==0) g.pushlist(); // horizontal
            
            scoregroup &sg = *groups[k];
            int bgcolor = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? 0x3030C0 : 0xC03030) : 0,
                fgcolor = 0xFFFF80;

            g.pushlist(); // vertical
            g.pushlist(); // horizontal

            #define loopscoregroup(o, b) \
                loopv(sg.players) \
                { \
                    fpsent *o = sg.players[i]; \
                    b; \
                }    

            g.pushlist();
            if(sg.team && m_teammode)
            {
                g.pushlist();
                g.background(bgcolor, numgroups>1 ? 3 : 5);
                g.strut(1);
                g.poplist();
            }
            g.text("", 0, "server");
            loopscoregroup(o,
            {
                if(o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1))
                {
                    g.pushlist();
                    g.background(0x808080, numgroups>1 ? 3 : 5);
                }
                const playermodelinfo &mdl = getplayermodelinfo(o);
                const char *icon = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? mdl.blueicon : mdl.redicon) : mdl.ffaicon;
                g.text("", 0, icon);
                if(o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1)) g.poplist();
            });
            g.poplist();

            if(sg.team && m_teammode)
            {
                g.pushlist(); // vertical

                if(sg.score>=10000) g.textf("%s: WIN", fgcolor, NULL, sg.team);
                else g.textf("%s: %d", fgcolor, NULL, sg.team, sg.score);

                g.pushlist(); // horizontal
            }

            if(showfrags)
            { 
                g.pushlist();
                g.strut(3);
                g.text("K", fgcolor);
                loopv(sg.players)
                {
                    fpsent *o = sg.players[i];
                    g.textf("%d", 0xFFFFDD, NULL, o->frags);
                }
                g.poplist();

                g.pushlist();
                g.strut(5);
                g.text("D", fgcolor);
                loopv(sg.players)
                {
                    fpsent *o = sg.players[i];
                    g.textf("%d", 0xFFFFDD, NULL, o->deaths);
                }
                g.poplist();
            }

            if(multiplayer(false) || demoplayback)
            {
                if(showpj)
                {
                    g.pushlist();
                    g.strut(6);
                    g.text("pj", fgcolor);
                    loopscoregroup(o,
                    {
                        if(o->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                        else g.textf("%d", 0xFFFFDD, NULL, o->plag);
                    });
                    g.poplist();
                }
        
                if(showping)
                {
                    g.pushlist();
                    g.text("ping", fgcolor);
                    g.strut(6);
                    loopscoregroup(o, 
                    {
                        fpsent *p = getclient(o->ownernum);
                        if(!p) p = o;
                        if(!showpj && p->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                        else g.textf("%d", 0xFFFFDD, NULL, p->ping);
                    });
                    g.poplist();
                }
            }

            g.pushlist();
            g.text("name", fgcolor);
            g.strut(10);

            char *name = (char *)calloc(250, 1);
            char *clan = (char *)calloc(250, 1);
            loopscoregroup(o, 
            {
                int status = o->state!=CS_DEAD ? 0xFFFFDD : 0x606060;
                if(o->privilege)
                {
                    status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o->state==CS_DEAD) status = (status>>1)&0x7F7F7F;
                }
                g.pushlist();
                if(colorclans)
                {
                    int color = createname(colorname(o), clan, name);
                    if(color != -1)
                    {
                        g.text(clan, color);
                        g.text(" ", 0xFFFFFF);
                        g.text(name, status);
                    }
                    else
                        g.text(colorname(o), status);
                }
                else
                     g.text(colorname(o), status);
                g.poplist();
            });
            free(name);
            free(clan);
            g.poplist();

            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.space(1);
                g.pushlist();
                g.text("cn", fgcolor);
                loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL, o->clientnum));
                g.poplist();
            }
            
            if(sg.team && m_teammode)
            {
                g.poplist(); // horizontal
                g.poplist(); // vertical
            }

            g.poplist(); // horizontal
            g.poplist(); // vertical

            if(k+1<numgroups && (k+1)%2) g.space(2);
            else g.poplist(); // horizontal
        }
        
        if(showspectators && spectators.length())
        {
            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.pushlist();
                
                g.pushlist();
                g.text("spectator", 0xFFFF80, "server");
                g.strut(10);
                char *name = (char *)calloc(250, 1);
                char *clan = (char *)calloc(250, 1);
                loopv(spectators) 
                {
                    fpsent *o = spectators[i];
                    int status = 0xFFFFDD;
                    if(o->privilege) status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    g.pushlist();
                    if(o==player1 && highlightscore)
                        g.background(0x808080, 3);
                    g.text("", 0xFFFFFF, "spectator");
                    if(colorclans)
                    {
                        int color = createname(colorname(o), clan, name);
                        if(color != -1)
                        {
                            g.text(clan, color);
                            g.text(" ", 0xFFFFFF);
                            g.text(name, status);
                        }
                        else
                            g.text(colorname(o), status);
                    }
                    else
                        g.text(colorname(o), status);
                    g.poplist();
                }
                free(clan);
                free(name);
                g.poplist();

                g.space(1);
                g.pushlist();
                g.text("cn", 0xFFFF80);
                loopv(spectators) g.textf("%d", 0xFFFFDD, NULL, spectators[i]->clientnum);
                g.poplist();

                if(showping)
                {
                    g.space(3);
                    g.pushlist();
                    g.text("ping", 0xFFFF80);
                    loopv(spectators)
                    {
                        fpsent *p = spectators[i];
                        if(!showpj && p->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                        else g.textf("%d", 0xFFFFDD, NULL, p->ping);
                    };
                    g.poplist();
                }


                g.poplist();
            }
            else
            {
                g.textf("%d spectator%s", 0xFFFF80, "server", spectators.length(), spectators.length()!=1 ? "s" : "");
                loopv(spectators)
                {
                    if((i%3)==0) 
                    {
                        g.pushlist();
                        g.text("", 0xFFFFDD, "spectator");
                    }
                    fpsent *o = spectators[i];
                    int status = 0xFFFFDD;
                    if(o->privilege) status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o==player1 && highlightscore)
                    {
                        g.pushlist();
                        g.background(0x808080);
                    }
                    g.text(colorname(o), status);
                    if(o==player1 && highlightscore) g.poplist();
                    if(i+1<spectators.length() && (i+1)%3) g.space(1);
                    else g.poplist();
                }
            }
        }
    }

    static int scoresort(const int *a, const int *b)
    {
        return *b-*a;
    }
    int getscores(vector<int> &v)
    {
        int numgroups = groupplayers();
        loopk(numgroups)
        {
            scoregroup &sg = *groups[k];
            if(sg.team && m_teammode)
                v.add(sg.score);
        }
        v.sort(scoresort);
        loopk(numgroups)
        {
            scoregroup &sg = *groups[k];
            if(isteam(player1->team, sg.team))
                return k;
        }
        return -1;
    }

    struct scoreboardgui : g3d_callback
    {
        bool showing;
        vec menupos;
        int menustart;

        scoreboardgui() : showing(false) {}

        void show(bool on)
        {
            if(!showing && on)
            {
                menupos = menuinfrontofplayer();
                menustart = starttime();
            }
            showing = on;
        }

        void gui(g3d_gui &g, bool firstpass)
        {
            g.start(menustart, 0.03f, NULL, false);
            renderscoreboard(g, firstpass);
            g.end();
        }

        void render()
        {
            if(showing) g3d_addgui(this, menupos, scoreboard2d ? GUI_FORCE_2D : GUI_2D | GUI_FOLLOW);
        }

    } scoreboard;

    void g3d_gamemenus()
    {
        scoreboard.render();
    }

    VARFN(scoreboard, showscoreboard, 0, 0, 1, scoreboard.show(showscoreboard!=0));

    void showscores(bool on)
    {
        showscoreboard = on ? 1 : 0;
        scoreboard.show(on);
    }
    ICOMMAND(showscores, "D", (int *down), showscores(*down!=0));
}

