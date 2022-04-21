/*************************************************************/
/* wizfs.c  v1.0                                             */
/* by adventuresin9                                          */
/*                                                           */
/* With the default options, this will post "wizfs" to /srv  */
/* and mount the file system in /n with a directory called   */
/* "wiz".                                                    */
/*                                                           */
/* /lib/ndb/local will be checked, and any line with         */
/* "wiz=bulb" will use the "sys=" entry to make a bulb file. */
/*                                                           */
/* Reading the bulb files will output the results from the   */
/* getPilot function of the Philips Wiz bulbs.               */
/*                                                           */
/* Writing will take "on", "off", and "pulse", as single     */
/* word commands, and report an error for others.  All other */
/* commands must be in a "key=value" format.  Errors from    */
/* the bulb about bad commands will be reported back.        */
/*                                                           */
/*************************************************************/



#include <u.h>
#include <libc.h>
#include <bio.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <ndb.h>
#include <json.h>

#define PILOT "{\"method\":\"getPilot\",\"params\":{}}"
#define SYSTEM "{\"method\":\"getSystemConfig\", \"params\":{}}"
#define PULSE "{\"method\":\"pulse\",\"params\":{\"delta\":-50,\"duration\":500}}"
#define WIZOFF "{\"method\": \"setPilot\", \"id\": 24, \"params\": {\"state\": false}}"
#define WIZON "{\"method\": \"setPilot\", \"id\": 24, \"params\": {\"state\": true}}"
#define NOBULB "Bulb unreachable ☹"

typedef struct Bulbcmd Bulbcmd;

struct Bulbcmd
{
	char *key;
	char *value;
};


void fsread(Req *r);
void fswrite(Req *r);
void fsstart(Srv*);
void fsend(Srv*);


Srv fs = 
{
	.start=fsstart,
	.read=fsread,
	.write=fswrite,
	.end=fsend,
};


File *root;
File *wizdir;
char *mtpt;
char *service;
int debug = 0;


void
timeout(void *, char *msg)
{
	if(strstr("alarm", msg) != nil)
		noted(NCONT);
	else
		noted(NDFLT);
}


int
callbulb(char *bulbname, char *bulbcmd, char *bulbreply, long brsize)
{
	int fd, n;
	n = 0;

	if(debug)
		print("bulbname %s\nbulbcmd %s\nmkaddr %s\n", bulbname, bulbcmd, netmkaddr(bulbname, "upd", "38899"));
	
	notify(timeout);
	alarm(1000);

	fd = dial(netmkaddr(bulbname, "udp", "38899"), nil, nil, nil);
	fprint(fd, bulbcmd);
	sleep(10);
	n = read(fd, bulbreply, brsize);

	alarm(0);

	if(debug)
		print("fd is %d\nbulbreply %s\n", fd, bulbreply);

	close(fd);
	return(n);
}


int
jtoresult(JSON *jreply, char *out, long nout)
{
	JSON *jresult;
	JSONEl *jp;
	char *p;
	int n = 0;

	jresult = jsonbyname(jreply, "result");

	jp = jresult->first;
	p = out;

	while(jp != nil){
		p = seprint(p, out + nout, jp->name);

		switch(jp->val->t){
		case JSONNull:
			p = seprint(p, out + nout, "=‽\n");
			break;
		case JSONBool:
			p = seprint(p, out + nout, "=%s\n", jp->val->n ? "true" : "false");
			break;
		case JSONNumber:
			p = seprint(p, out + nout, "=%.f\n", jp->val->n);
			break;
		case JSONString:
			p = seprint(p, out + nout, "=%s\n", jp->val->s);
			break;
		}
		n++;
		jp = jp->next;
	}

	USED(p);
	USED(jp);
	return(n);
}


int
jtoerror(JSON *jreply, char *error)
{
	JSON *jerror;
	JSON *jmsg;
	int n = 0;

	jerror = jsonbyname(jreply, "error");

	if(jerror != nil)
		jmsg = jsonbyname(jerror, "message");

	if(jmsg != nil){
		strcpy(error, jmsg->s);
		n = strlen(error);
	}

	return(n);
}


int
makebulbcmd(char *input, char *out, long nout)
{
	Bulbcmd bc[16];

	char *pair[16];
	char *command[2];
	char *p;

	int i, ti, ci;

	if(debug)
		print("input %s\n", input);

	ti = tokenize(input, pair, 16);

	if(!ti)
		return(ti);

	for(i = 0; i < ti; i++){
		ci = getfields(pair[i], command, 2, 1, "=");
		if(ci == 2){
			bc[i].key = command[0];
			bc[i].value = command[1];
		}
		else{
			bc[i].key = command[0];
			if(!strcmp(bc[0].key, "pulse")){
				strcpy(out, PULSE);
				return(1);
			}else if(!strcmp(bc[0].key, "on")){
				strcpy(out, WIZON);
				return(1);
			}else if(!strcmp(bc[0].key, "off")){
				strcpy(out, WIZOFF);
				return(1);
			}
			return(0);
		}
	}

	p = out;
	p = seprint(p, out + nout, "{\"method\":\"setPilot\",\"id\":%d,\"params\":{", time(0));

	i = 0;
	while(i < (ti - 1)){
		p = seprint(p, out + nout, "\"%s\":%s,", bc[i].key, bc[i].value);
		i++;
	}

	p = seprint(p, out + nout, "\"%s\":%s}}", bc[i].key, bc[i].value);

	USED(p);

	return(strlen(out));
}


void
fsmkdir(void)
{
	Ndb *bulbndb;
	Ndbs s;
	Ndbtuple *bulbtp;
	char *sysname;
	char *user;

	/* this assumes the system name "sys=" is the first entry for a line in ndb/local */
	/* all bulbs on the network must have "wiz=bulb" to be included in the file system */

	user = getuser();
	fs.tree = alloctree(user, user, 0555, nil);

	root = fs.tree->root;

	wizdir = createfile(root, "wiz", user, DMDIR|0555, nil);

	bulbndb = ndbopen(0);

	for(bulbtp = ndbsearch(bulbndb, &s, "wiz", "bulb"); bulbtp != nil; bulbtp = ndbsnext(&s, "wiz", "bulb")){
		sysname = bulbtp->val;
		createfile(wizdir, sysname, user, 0666, nil);
	}

	ndbclose(bulbndb);
}


void
fsread(Req *r)
{
	char bulbreply[1024];
	JSON *jreply;
	char waserror[64];
	char *rerror;
	char *bulbname;
	char readout[1024];

	memset(bulbreply, 0, 1024);
	memset(readout, 0, 1024);
	rerror = nil;

	bulbname = r->fid->file->name;

	if(callbulb(bulbname, PILOT, bulbreply, sizeof(bulbreply)) < 1){
		respond(r, NOBULB);
		return;
	}

	jreply = jsonparse(bulbreply);

	if(jtoresult(jreply, readout, sizeof(readout)) < 1){
		jtoerror(jreply, waserror);
		rerror = waserror;
	}

	readstr(r, readout);
	jsonfree(jreply);

	respond(r, rerror);
}



void
fswrite(Req *r)
{
	int n;
	char *input;
	char *rerror;
	char waserror[64];
	char bulbcmd[1024];
	char bulbreply[1024];
	JSON *jreply;

	memset(bulbcmd, 0, 1024);
	memset(bulbreply, 0, 1024);
	rerror = nil;

	n = r->ofcall.count = r->ifcall.count;
	input = emalloc9p(n+1);
	memmove(input, r->ifcall.data, n);

	if(makebulbcmd(input, bulbcmd, sizeof(bulbcmd)) < 1){
		respond(r, "makebulbcmd failed");
		free(input);
		return;
	}

	if(callbulb(r->fid->file->name, bulbcmd, bulbreply, sizeof(bulbreply)) < 1){
		respond(r, NOBULB);
		free(input);
		return;
	}

	jreply = jsonparse(bulbreply);

	if(jtoerror(jreply, waserror))
		rerror = waserror;

	respond(r, rerror);
	free(input);
	jsonfree(jreply);
}


void
fsstart(Srv*)
{
	fsmkdir();
}


void
fsend(Srv*)
{
	postnote(PNGROUP, getpid(), "shutdown");
	threadexitsall(nil);
}


void
usage(void)
{
	fprint(2, "usage: %s [-d] [-m mtpt] [-s service]\n", argv0);
	exits("usage");
}


void
threadmain(int argc, char *argv[])
{
	mtpt = "/n";
	service = "wizfs";

	ARGBEGIN {
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 's':
		service = EARGF(usage());
		break;
	case 'd':
		debug++;
		break;
	default:
		usage();
	} ARGEND;

	threadpostmountsrv(&fs, service, mtpt, MREPL|MCREATE);
	threadexits(nil);
}
