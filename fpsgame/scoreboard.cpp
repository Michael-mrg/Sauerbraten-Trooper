// creation of scoreboard
#include "game.h"

namespace game
{
    VARP(scoreboard2d, 0, 1, 1);
    VARP(showclientnum, 0, 0, 1);
    VARP(showpj, 0, 0, 1);
    VARP(showping, 0, 1, 1);
    VARP(showspectators, 0, 1, 1);
    VARP(highlightscore, 0, 1, 1);
    VARP(showconnecting, 0, 0, 1);
    
    VARP(showfrags, 0, 1, 1);
    VARP(scoreboardcolumns, 0, 1, 1);
    VARP(highlighttopfraggers, 0, 1, 1);
    ICOMMAND(highlight, "i", (int *cn), loopv(players) if(players[i]->clientnum == *cn) players[i]->highlight |= 0x10;);
    ICOMMAND(unhighlight, "i", (int *cn), loopv(players) if(players[i]->clientnum == *cn) players[i]->highlight &= 0x01;);
    VARP(highlightall, 0, 0, 1);

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
        if((*a)->deaths > (*b)->deaths) return 1;
        if((*a)->deaths < (*b)->deaths) return -1;
        return strcmp((*a)->name, (*b)->name);
    }

    void getbestplayers(vector<fpsent *> &best)
    {
        getsortedplayers(best);
        while(best.length()>1 && best.last()->frags < best[0]->frags) best.drop();
    }

    void getsortedplayers(vector<fpsent *> &best)
    {
        loopv(players)
        {
            fpsent *o = players[i];
            if(o->state!=CS_SPECTATOR) best.add(o);
        }
        best.sort(playersort);
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
        if(michaelmods && highlighttopfraggers)
            loopk(numgroups)
                if(!isteam(player1->team, groups[k]->team))
                {
                    loopv(groups[k]->players)
                        groups[k]->players[i]->highlight &= 0x10;
                    loopi(ceil(groups[k]->players.length() / 10.0))
                        groups[k]->players[i]->highlight |= 0x01;
                }
        return numgroups;
    }
    
    static int scoresort(int *a, int *b) { return *b - *a; }
    int getscores(vector<int> &v)
    {
        int n = groupplayers();
        loopk(n)
            if(groups[k]->team && m_teammode)
                v.add(groups[k]->score);
        v.sort(scoresort);
        loopk(n)
            if(isteam(player1->team, groups[k]->team))
                return k;
        return -1;
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
        int cols = michaelmods && scoreboardcolumns ? (numgroups > 2 ? 3 : numgroups) : (numgroups > 1 ? 2 : 1);
        loopk(numgroups)
        {
            if((k%cols)==0) g.pushlist(); // horizontal
            
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

            bool b = michaelmods && showfrags;
            if(!cmode || !cmode->hidefrags() || b)
            {
                g.pushlist();
                g.strut(b ? 3 : 7);
                g.text("K", fgcolor);
                loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL, o->frags));
                g.poplist();
                
                if(b)
                {
                    g.pushlist();
                    g.strut(5);
                    g.text("D", fgcolor);
                    loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL, o->deaths));
                    g.poplist();
                }
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
            loopscoregroup(o, 
            {
                int status = o->state!=CS_DEAD ? 0xFFFFDD : 0x606060;
                if(o->privilege)
                {
                    status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o->state==CS_DEAD) status = (status>>1)&0x7F7F7F;
                }
                g.text(colorname(o), status);
            });
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

            if(k+1<numgroups && (k+1)%cols) g.space(2);
            else g.poplist(); // horizontal
        }
        
        int len = spectators.length();
        cols = (michaelmods && scoreboardcolumns) ? ((cols >= 3) ? 5 : cols + 1) : 1;
        if(showspectators && len)
        {
            #define loopspecgroup(o, start, end, b) \
                for(int i = start; i < min(end, len); i ++) \
                { \
                    fpsent *o = spectators[i]; \
                    b; \
                }
            int index = 0;
            g.pushlist();
            for(int c = 0; c < cols, index < len; c ++)
            {
                int stride = ceil((float)(len - index) / (cols - c));
                g.pushlist();
                g.pushlist();
                
                g.pushlist();
                g.text("spectator", 0xFFFF80, "server");
                loopspecgroup(o, index, index+stride, {
                    g.pushlist();
                    int status = 0xFFFFDD;
                    if(o->privilege)
                        status = o->privilege >= PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o == player1 && highlightscore)
                        g.background(0x808080, 3);
                    g.text(colorname(o), status, "spectator");
                    g.poplist();
                });
                g.poplist();
                
                if(showclientnum || player1->privilege>=PRIV_MASTER)
                {
                    g.space(1);
                    g.pushlist();
                    g.text("cn", 0xFFFF80);
                    loopspecgroup(o, index, index+stride, {
                        g.textf("%d", 0xFFFFDD, NULL, o->clientnum);
                    });
                    g.poplist();
                }
                
                if(showping)
                {
                    g.space(2);
                    g.pushlist();
                    g.text("ping", 0xFFFF80);
                    loopspecgroup(o, index, index+stride, {
                        g.textf("%d", 0xFFFFDD, NULL, o->ping);
                    });
                    g.poplist();
                }
                              
                if(c != cols - 1)
                    g.space(3);
                g.poplist();
                g.poplist();
                
                index += stride;
            }
            g.poplist();
        }
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

