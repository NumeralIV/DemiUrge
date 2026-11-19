// DEMIURGE — Sim/Names.cpp : procedural languages and names (SPEC-SIM 3.1, DM.Names).
// Owner: names engineer (key "names"; rewritten by the names-cultures track, 2026-11-19).
// Design document: docs/content/cultures.md. The eight culture families there are the data tables below.
//
// A language is an opaque text record, "dm2;key=value;...", stored as Culture::lang and saved as is.
// It names its family (0..7), its generation, the sound laws it went through, its own inventories
// (sounds with weights), its recurring roots, place endings, prefixes, person endings, ruler titles,
// ship words and its word for the player-god. Names parses it on every call (no globals, no caches).
// Older "dm1" records (saves from before this track) and unreadable text become a language derived
// from a hash of the text, so they keep naming deterministically.
//
// Three ideas carry the design:
//  1. Families. Every family has its own sounds, syllable shapes, stress, forbidden sequences and naming
//     patterns, so a family is recognisable from a few place names.
//  2. Regular sound laws. A daughter language applies one or two of its family's laws to everything it
//     inherits (sounds, roots, endings, the god's name), so daughters look related but distinct.
//  3. A spelling code. Every place, person and people name is spelled so that the values of its letters
//     (a = 1 .. z = 26) sum to kTarget modulo 27. Two such names can never differ by one substitution,
//     insertion or deletion (that would change the sum by 1..26, never by 27), so no world, whatever its
//     languages, holds near-twins at edit distance 1. The last syllable of each name is the free slot:
//     it is drawn among every syllable the language allows whose letters make the sum come out right.
// Every random draw comes from the Rng argument, and every container is walked in index order.
#include "Names.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dm::names {
namespace {

// ============================================================ letters and the spelling code

bool isVowelCh(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }
bool isLetterCh(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lowerCh(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
bool isSep(char c) { return c == ' ' || c == '-' || c == '\''; }

constexpr int kMod = 27;      // letter values 1..26 are all distinct and non-zero modulo 27
constexpr int kTarget = 11;   // every coded name's letter sum, modulo kMod

int letterVal(char c) {
	const char l = lowerCh(c);
	return (l >= 'a' && l <= 'z') ? int(l - 'a') + 1 : 0;
}
int modN(int x) {
	const int m = x % kMod;
	return m < 0 ? m + kMod : m;
}
int residOf(std::string_view s) {
	int t = 0;
	for (char c : s) t += letterVal(c);
	return t % kMod;
}

std::string lettersLower(std::string_view s) {
	std::string r;
	for (char c : s) {
		if (isLetterCh(c)) r += lowerCh(c);
	}
	return r;
}

// Vowel runs of a word: its syllable nuclei (a diphthong is one nucleus). Placeholders A O U count.
int nucleiOf(std::string_view w) {
	int n = 0;
	bool inV = false;
	for (char c : w) {
		const char l = lowerCh(c);
		const bool v = isVowelCh(l);
		if (v && !inV) n++;
		inV = v;
	}
	return n;
}

// ============================================================ the eight families (docs/content/cultures.md)

struct FamilyDef {
	const char* key;        // short id (docs, tests)
	const char* on;         // word-initial onsets, token:weight
	const char* om;         // onsets inside a word
	const char* vo;         // vowels (families without harmony)
	const char* fr;         // front vowels (harmony)
	const char* bk;         // back vowels (harmony; i is neutral)
	const char* lo;         // long vowels and diphthongs: only in the stressed syllable
	const char* cm;         // codas inside a word
	const char* cf;         // codas at the end of a word
	int pe0, pe1, pcm, pcf, pl, pg;   // per mille: empty first onset, empty inner onset (hiatus or glottal stop),
	                                  // inner coda, final coda, long nucleus when stressed, doubling after the stress
	const char* gem;        // consonants that may double after the stressed vowel
	const char* vv;         // vowel pairs allowed across a syllable boundary
	bool harmony;           // a word is all front (e, i) or all back (a, o, u, with neutral i)
	bool apos;              // a glottal stop between vowels, written '
	int stress;             // 0 initial, 1 penultimate, 2 final, 3 antepenultimate
	int syl1, syl2, syl3;   // weights of a place's specific part having 1, 2 or 3 syllables
	int fGen, fBare, fTwo, fHyph, fRedup, fPre;   // place form weights
	int pRoot;              // per mille: a place's specific part is one of the language's recurring roots
	const char* forbid;     // forbidden letter sequences ("VV": any two vowels side by side)
	const char* gens;       // place endings (generic elements); A O U are harmonic vowels
	const char* pres;       // first words of two-word places (fTwo) or bound prefixes (fPre)
	const char* pends;      // person endings, token:weight
	const char* nameEnd;    // a people's name ending, token:weight ("_" = none)
	const char* adjEnd;     // adjective endings, token:weight
	const char* plurEnd;    // plural ending ("" = the plural is the name)
	const char* godRoots;   // divine roots
	const char* godEnds;    // divine endings
	const char* titles;     // ruler titles in the family's tongue
	const char* epithets;   // English bynames, separated by ';'
	const char* ships;      // first words of ship names
	const char* laws;       // the sound laws its daughters may take (ids of kLaws)
	int sister;             // the family it borrows from in contact
	int maxWord;            // letters in one word of a place name (two-word names: each word)
};

constexpr int kFamilies = 8;

const FamilyDef kFam[kFamilies] = {
	// 0 VELESSAN: the harbour tongues of the warm coasts. Liquid, open, vowel-final; doubled consonant after
	// the stressed (penultimate) vowel; place endings -essa, -enna, -ara. "Velessa", "Tavorra", "Lusa Amarenne".
	{ "velessan",
	  "l:4,m:3,n:2,r:2,s:4,v:4,t:3,d:3,b:2,f:2,p:2,g:1,tr:1,pr:1,br:1,fl:1",
	  "l:5,m:3,n:4,r:4,s:3,v:3,t:3,d:3,f:1,b:1,p:1,g:1",
	  "a:5,e:4,i:3,o:4,u:1", "", "", "ia:2,io:1,ea:1",
	  "l:3,n:3,r:3,s:2", "n:3,l:2,r:2,s:2",
	  120, 0, 180, 160, 90, 300, "lnrstm", "", false, false, 1,
	  4, 5, 0, 60, 26, 14, 0, 0, 0, 200,
	  "aa,ee,ii,oo,uu,ae,ao,au,ei,eu,ie,oa,oe,oi,ou,ua,ue,ui,uo,nr,nl,lr,rl,sr,ml,mr,ln,tl,dl,vt,dt,td,tv,dv,vd,sd,ds,bv,vb,fv,vf,pb,bp,gd,dg,tg,gt,pt,tp,mn,nm,vl,vn,vm,fn,dn,tn,bn,pn,gn,dm,tm,bm,pm,gm,fm,sv,vs,rs,ls,ns,sf,sg,sb,lf,nf,rf,nb,nv,lb,lg,ng,ngr,rtr,ltr,str,spr,nfl,lfl,rfl,sfl,sm,sn,lm",
	  "essa,ara,enna,ossa,ella,ino,anto,orra,ivia,alle,isca,onde,ora,ile,etta,aro,elia,anne,iro,esca,ovia,arra",
	  "lusa,sanne,dovra,amme,tella,viro",
	  "o:3,a:3,io:2,eo:1,ina:2,ano:2,ello:1,ia:2,etto:1,ene:1,ira:1",
	  "_:2,a:3,e:1", "an:3,ese:2,ine:1", "i",
	  "aul,ilv,oren,vael,lum,sar,ombr", "ione,are,ello,ia,esse,iro",
	  "ossaro,tavenne,vessaro,doravo",
	  "the Tide-Born;the Fair-Sailing;the Vine-Crowned;the Golden-Voiced;the Gentle;the Far-Faring;the Lamp-Bearer;the Harbour-Builder;the Laughing;the Sea-Wed",
	  "lusa,ondessa,silvara,morenna,vellara",
	  "1,5,13,4,16,30,11", 7, 11 },

	// 1 KORVAGH: the hard tongues of the steppe and the high passes. Closed syllables, back vowels,
	// kh gh zh, clusters, initial stress, hyphenated clan compounds. "Durvakh", "Tor-Uzgal", "Ghazmor".
	{ "korvagh",
	  "k:3,g:3,d:3,t:2,b:3,r:2,v:2,z:1,kh:3,gh:2,zh:1,m:1,n:1,s:1,h:1,dr:2,gr:2,kr:2,br:1,tr:1,vr:1,khr:1",
	  "k:2,g:3,d:3,t:2,b:2,r:3,v:2,z:1,kh:2,gh:2,zh:1,m:1,n:2,l:2",
	  "a:4,o:4,u:4,e:1,i:1", "", "", "",
	  "r:4,z:1,g:2,k:2,n:2,m:1,d:1,l:1", "k:3,r:3,d:2,g:2,z:1,kh:3,gh:3,n:1,m:1,rk:1,rg:1,rd:1,zd:1,sk:1",
	  150, 0, 350, 850, 0, 0, "", "", false, false, 0,
	  3, 5, 0, 58, 24, 0, 18, 0, 0, 200,
	  "VV,ghk,khg,kgh,gkh,khkh,ghgh,khgh,ghkh,zhz,zzh,ghz,zgh,kk,gg,dd,tt,bb,zz,rr,mm,nn,ll,hk,hg,hd,hb,ht,hz,hm,hn,hl,hv,dh,bh,th,rh,nh,mh,vh,lh,td,dt,gk,kg,bp,pb,db,bd,gb,bg,dg,gd,vz,zv,zs,sz,shk,ghr,zhr,mr,nr,lr,rl,ln,nl,zr,zl,rzh,nzh,mzh,gzh,dzh,kzh,bzh,vzh,zhg,zhk,zhd,zhb,zhm,zhn,zhv",
	  "dar,mor,ukh,gul,tash,zog,drun,vakh,orn,bod,rak,dum,ghaz,kor,uzd,argh,osk,druk,maz,gat,ubar,oz",
	  "",
	  "ak:2,uk:2,ar:2,osh:1,ag:1,ur:2,od:1,ug:1,az:1,um:1",
	  "_:1", "i:1", "ar",
	  "ukh,gor,ard,bur,khor,vog,zhar", "tar,um,agh,od,ukh",
	  "ughtar,dravokh,zhogan,borukh",
	  "the Iron-Handed;the Unbowed;the Horse-Lord;the Stone-Breaker;the Grim;the Red-Axe;the Far-Rider;the Hammer;the Wolf-Eyed;the Thunder-Voiced",
	  "ughrak,morkh,zagra,durvakh",
	  "6,11,12,15,27,35", 6, 10 },

	// 2 AAVIK: the harmonic tongues of the northern lakes. Vowel harmony, long vowels written double in the
	// first (stressed) syllable, doubled consonants after it, no onset clusters, long words. "Kaarilanna".
	{ "aavik",
	  "k:3,t:3,h:3,m:2,n:2,p:2,r:1,s:3,v:3,j:1,l:2",
	  "k:3,t:3,h:1,m:2,n:2,p:2,r:3,s:3,v:2,j:1,l:4",
	  "", "e:3,i:3", "a:4,o:3,u:3,i:1", "aa:3,oo:2,uu:2,ai:2,au:1,ou:1,uo:1,oi:1,ee:2,ii:2,ei:2,ie:1",
	  "n:3,l:3,r:2,s:2,t:1,k:1", "n:2,s:1,t:1",
	  180, 0, 260, 120, 330, 350, "ktplnsm", "", true, false, 0,
	  3, 5, 1, 70, 30, 0, 0, 0, 0, 200,
	  "hj,jh,vj,jv,ji,lj,sj,tj,kj,pj,nj,mj,nm,mn,ae,ao,ea,eo,eu,ia,io,iu,oa,oe,ua,ue,ui,hk,hp,hs,hv,hl,hn,hm,hr,kh,th,ph,sh,ns,ls,rs,ts,ks,sr,sl,sn,sm,np,mp,nk,tk,kt,pt,tp,lr,rl,nr,nl,ln,rn,mr,ml,rm,km,kn,tn,pn,tm,pm,vk,vt,vp,vs,vm,vn,vl,vr",
	  "lAnnA,vAntO,rOsti,mAkki,sUlmi,kOrpi,jArvO,lAhtO,hOlmi,pAkkA,nUmmi,sAlO,tOlA,rAntA,vUOtO,kAisU,tAistO,rAvilA,kUUsi,hArjU",
	  "",
	  "O:3,i:3,A:2,kki:1,mO:1,jA:1,ri:1,tti:1",
	  "A:3,O:1", "n:1", "t",
	  "halv,sier,kuuv,erv,tuov,aarn", "AtAr,O,kki,rAs",
	  "vaalto,kuura,hallava",
	  "the Frost-Born;the Patient;the Lake-Singer;the Birch-Crowned;the Long-Wintered;the Rune-Wise;the Quiet;the Far-Hearing;the Reindeer-Keeper;the Snow-Eyed",
	  "ahvo,kuulas,raavi,sarka",
	  "14,28,3,22,29,4", 6, 12 },

	// 3 QA'TIRI: the desert tongues of the oasis cities. Only a, i, u; q kh sh z h; glottal stops written ';
	// long vowels in the stressed final syllable; place endings -arun, -isham, -aqim. "Qa'tirun", "Saru Dinaal".
	{ "qatiri",
	  "q:3,k:1,kh:2,sh:3,s:3,z:2,h:3,t:2,d:3,r:1,n:2,m:3,b:1,j:1,f:1,y:1",
	  "q:2,kh:2,sh:2,s:2,z:1,h:2,t:2,d:2,r:4,n:3,m:2,b:2,f:1,l:3",
	  "a:6,i:4,u:3", "", "", "aa:4,ii:1,uu:2",
	  "r:3,n:2,m:2,sh:2,z:2,b:1,d:1,q:1,h:1", "r:3,n:4,m:2,sh:2,z:2,d:1,q:1,b:1,t:1",
	  200, 60, 400, 550, 180, 0, "", "", false, true, 2,
	  3, 5, 0, 55, 30, 15, 0, 0, 0, 200,
	  "e,o,qk,kq,hh,shh,zs,sz,zsh,shz,qq,kk,td,dt,tq,qt,hq,qh,dq,qd,bq,qb,hy,yh,yi,hk,hr,rh,nh,mh,zh,dh,bh,th,jh,fh,lh,shd,shq,shn,shm,shb,shz,shr,shl,rq,nq,mq,zq,dz,zd,tz,zt,nb,mb,nm,mn,bn,bm,db,bd,lr,rl,ln,nl,jq,fq,yq,qj,qf,qy,aaa,iii,uuu",
	  "arun,isham,udar,aqim,azir,iyun,anuq,ahar,ushta,imar,abir,uzan,irun,aqar,unab,ishan,zuun,tibar,amil,urad",
	  "qasu,ishad,saru,mahz,tiru,dimar",
	  "ad:2,un:2,ir:2,ia:1,ima:2,uz:1,an:2,it:1,ash:1",
	  "_:1", "i:1", "un",
	  "il,shu,nur,aq,zar,hab", "ash,ima,unar,ihun,ub",
	  "qadun,ishar,zahar,hamzir",
	  "the Star-Reader;the Patient;the Well-Finder;the Veiled;the Thrice-Wise;the Sand-Walker;the Generous;the Silent;the Lantern of the Dunes;the Caravan-Master",
	  "shubara,iyaru,nubira,qamira",
	  "20,7,21,14,5,1", 5, 11 },

	// 4 HANUWE: the island tongues of the reefs. Nine consonants, open syllables only, vowel pairs, and
	// reduplication. "Lelemoa", "Kahiwane", "Tuawekele".
	{ "hanuwe",
	  "h:3,k:3,l:3,m:3,n:3,p:2,t:3,w:3,ng:1",
	  "h:2,k:3,l:4,m:3,n:3,p:2,t:3,w:2,ng:1",
	  "a:5,e:3,i:3,o:4,u:4", "", "", "",
	  "", "",
	  200, 160, 0, 0, 0, 0, "", "ai,au,ae,ao,ei,eo,oa,oe,ou,ua,ue,ui,ia,io,iu,eu", false, false, 1,
	  2, 6, 1, 48, 38, 0, 0, 14, 0, 150,
	  "ngng,wu,hh,ww,uw,ngw,ngh,ngk,ngl,ngm,ngn,ngp,ngt",
	  "kele,lehu,hiwa,wane,tola,pilo,nahe,mena,hoki,niwa,tewa,pako,wiko,hume,lana,poki,moa,wae",
	  "",
	  "a:3,o:2,ewa:1,ono:1,ahi:1,ele:1,ua:1",
	  "_:1", "an:1", "",
	  "mahe,kowa,uhi,pela,awi", "walu,iwi,ao,eke,ani",
	  "hekau,pokane,lewahi",
	  "the Wave-Walker;the Star-Steerer;the Reef-Born;the Many-Islanded;the Child of Fire;the Long-Voyager;the Shell-Crowned;the Laughing;the Wind-Reader;the Net-Mender",
	  "kalowa,hinemo,tewaha,maheke",
	  "24,18,25,26,4,11", 0, 10 },

	// 5 MBENDI: the forest and savanna tongues. Prenasal onsets mb nd ng nj nz (also at the start of a word),
	// open syllables, bound place prefixes u-, ki-, ma-. "Mbaleko", "Kindembe", "Ndozimba".
	{ "mbendi",
	  "mb:3,nd:3,ng:2,nj:1,nz:1,b:1,d:1,k:3,t:2,l:2,m:2,n:1,s:2,w:2,y:1,ny:1,kw:1,gw:1,z:1",
	  "mb:2,nd:2,ng:2,nj:1,nz:1,k:3,t:2,l:4,m:2,n:2,s:2,w:2,y:1,ny:1,z:1,b:1,d:1",
	  "a:5,e:3,i:4,o:3,u:4", "", "", "",
	  "", "",
	  150, 0, 0, 0, 0, 0, "", "", false, false, 1,
	  2, 6, 0, 54, 30, 0, 0, 4, 12, 200,
	  "mbm,ndn,ngng,ngn,mbmb,ndnd,wu,yi,kwu,gwu,ww,yy,nyi,nyny,ngw",
	  "ndu,mbe,ngo,lela,mbola,ndaba,ngwe,yenga,zimba,mbuka,tende,njiri,landa,kumbe,ngoli,mbiri,wazi,ndeni",
	  "u,ki,ma,e",
	  "a:3,e:2,o:2,i:2,isa:1,ende:1,ombe:1,ulu:1,ika:1",
	  "a:2,o:1,i:1", "an:2,i:1", "ni",
	  "mbal,ndem,yel,kwan,zumb,tob", "u,ari,ende,ondo,iwe",
	  "mbaku,ndalo,ngozu",
	  "the Rain-Caller;the Drum-Voiced;the Lion-Hearted;the Elder;the Many-Herded;the Forest-Born;the Peacemaker;the Ironsmith;the Honey-Tongued;the River-Wise",
	  "mbeli,ngayo,sumbe,ndowa",
	  "17,31,25,32,4,11", 3, 10 },

	// 6 THRASK: the fjord tongues of the cold coasts. h- clusters (hr, hl, hv, thr), sk st sv, heavy codas
	// (rd rn ld nd), initial stress, short compounds. "Skeldvard", "Hrothbrekk", "Ulfheid".
	{ "thrask",
	  "hr:2,hl:1,hv:1,thr:2,th:2,sk:3,st:2,sv:2,kn:1,gr:1,br:1,fr:1,fl:1,b:2,d:2,g:2,h:2,k:2,r:1,s:2,t:1,v:2,f:2,m:1,n:1",
	  "d:2,g:2,k:2,l:3,m:1,n:2,r:3,s:2,t:2,v:2,f:1,th:2,b:1",
	  "a:4,e:3,i:3,o:3,u:3", "", "", "ei:2,au:2",
	  "r:4,l:3,n:2,s:2,k:1,g:1,d:1", "rd:2,rn:2,ld:2,lf:1,nd:2,rk:1,ng:1,st:2,r:2,n:2,k:1,d:1,m:1,sk:1,rg:1,lm:1",
	  150, 0, 420, 800, 120, 120, "lnkt", "", false, false, 0,
	  5, 3, 0, 66, 34, 0, 0, 0, 0, 200,
	  "ae,ai,ao,ea,eo,eu,ia,ie,io,iu,oa,oe,oi,ou,ua,ue,ui,uo,aa,ee,ii,oo,uu,hrh,thth,sks,dt,td,gk,kg,fv,vf,vth,thv,dth,tth,rdr,ndn,ldl,ngv,ngd,ngt,ngk,ngf,ngth,rnth,ldth,rdth,ndth,lfth,stth,skth,lfv,lmb,lmv,vd,vg,vk,vt,dk,dg,tg,gt,kd,gd,kf,gf,df,tf,mf,nf",
	  "vard,skel,brekk,heid,rond,skar,fjar,hvarn,eid,staf,ung,irn,osk,hald,seld,thurn,vang",
	  "",
	  "ar:2,ir:2,a:2,und:1,eld:1,run:1,vi:1,mar:1,dis:1",
	  "_:1", "ic:1", "ir",
	  "hrav,skeld,vald,throm,geir,ulf", "ung,ar,ir,und,adr",
	  "hersk,thrund,jorvald",
	  "the Oath-Keeper;the Fair-Haired;the Wave-Breaker;the Grey-Cloaked;the Bold;the Ring-Giver;the Frost-Bearded;the Wolf-Friend;the Skald-Beloved;the Storm-Rider",
	  "brimra,hvessa,skeldra,ulfvar",
	  "19,8,33,15,22,23", 1, 10 },

	// 7 PHERAXI: the old imperial tongue. ph th x, clusters pr tr str, antepenultimate stress, stately place
	// endings -ion, -eum, -axis, -ostra. "Pheraxis", "Thessalion", "Kalloneum".
	{ "pheraxi",
	  "ph:3,th:3,k:3,t:2,d:2,p:2,l:2,m:2,n:2,s:2,x:1,r:1,kl:1,kr:1,pr:2,tr:1,str:1,pl:1,h:1",
	  "ph:2,th:3,k:2,t:2,d:2,p:1,l:3,m:2,n:3,s:2,x:1,r:3,kr:1,tr:1",
	  "a:4,e:4,i:3,o:4,u:1", "", "", "ai:1,ei:2,oi:1,eu:2,ou:1",
	  "s:3,n:3,r:2,l:2", "s:4,n:3,x:2,r:1",
	  180, 40, 280, 450, 150, 120, "slnr", "ea,eo,ia,io", false, false, 3,
	  3, 5, 1, 74, 26, 0, 0, 0, 0, 200,
	  "xs,sx,xx,phph,thth,php,pph,thx,xth,phx,xph,xm,mx,xn,xr,xl,phth,thph,uu,ii,aa,oo,ee,ae,ao,au,ie,iu,oa,oe,ua,ue,ui,uo,xk,kx,xt,tx,xd,dx,xp,px,sr,lr,rl,nr,nl,ln,ml,mr,lm,nk,np,rk,lk,mk,rp,lp,sp,xax,xex,xix,xox,xux,rnp,nsp,ndr,rkr,nkr",
	  "ion,eum,axis,onos,eira,anthe,ostra,alos,imos,erax,enth,ophor,eron,phane,thalos,mene",
	  "",
	  "os:3,ia:2,on:2,ides:1,ax:1,eus:1,ra:1,ine:1,is:2",
	  "_:1", "ian:3,ine:1", "oi",
	  "kryth,elax,onth,istr,thal", "ion,eus,ara,on,ios",
	  "ostrarch,thalarch,exarion",
	  "the Lawgiver;the Builder;the Philosopher;the Victorious;the Golden-Tongued;the Just;the Restorer;the Saviour of Cities;the Learned;the Purple-Born",
	  "phaelon,erathe,ostrine,dorantha",
	  "9,8,10,34,22,1", 0, 12 },
};

// Bynames any family may use (the chronicle's translated voice).
const char* const kCommonEpithets = "the Wise;the Great;the Elder;the Younger;the Builder;the Pious;the Unlucky;the Twice-Crowned;the Peaceful;the Bold;the Silent;the Old";

// ============================================================ regular sound laws (daughter languages)

// where: 'A' anywhere, 'I' at the start of a word, 'V' between vowels, 'F' at the end of a word.
struct LawRule {
	int id;
	const char* from;
	const char* to;
	char where;
};

const LawRule kLaws[] = {
	{ 0, "k", "h", 'I' },
	{ 1, "t", "d", 'V' },
	{ 2, "p", "f", 'I' },
	{ 3, "s", "h", 'I' },
	{ 4, "e", "i", 'A' },
	{ 5, "b", "v", 'V' },
	{ 6, "g", "gh", 'V' },
	{ 7, "kh", "h", 'A' },
	{ 8, "th", "t", 'A' },
	{ 9, "ph", "f", 'A' },
	{ 10, "x", "s", 'A' },
	{ 11, "u", "o", 'A' },
	{ 12, "o", "u", 'A' },
	{ 13, "ss", "s", 'A' },
	{ 14, "aa", "a", 'A' }, { 14, "ee", "e", 'A' }, { 14, "ii", "i", 'A' }, { 14, "oo", "o", 'A' }, { 14, "uu", "u", 'A' },
	{ 15, "d", "t", 'F' },
	{ 16, "s", "r", 'V' },
	{ 17, "mb", "m", 'A' }, { 17, "nd", "n", 'A' },
	{ 18, "w", "v", 'A' },
	{ 19, "hr", "r", 'I' }, { 19, "hl", "l", 'I' }, { 19, "hv", "v", 'I' },
	{ 20, "q", "k", 'A' },
	{ 21, "sh", "s", 'A' },
	{ 22, "ei", "e", 'A' },
	{ 23, "au", "o", 'A' },
	{ 24, "t", "k", 'A' },
	{ 25, "l", "r", 'A' },
	{ 26, "ng", "n", 'A' },
	{ 27, "z", "zh", 'A' },
	{ 28, "kk", "k", 'A' }, { 28, "tt", "t", 'A' }, { 28, "pp", "p", 'A' },
	{ 29, "oo", "uo", 'A' }, { 29, "ee", "ie", 'A' },
	{ 30, "a", "e", 'F' },
	{ 31, "k", "ch", 'I' },
	{ 32, "d", "l", 'V' },
	{ 33, "sk", "sh", 'I' },
	{ 34, "s", "", 'F' },
	{ 35, "b", "p", 'I' },
};
constexpr int kLawRules = int(sizeof(kLaws) / sizeof(kLaws[0]));
constexpr int kMaxLaws = 8;   // sound laws a language keeps in its history (later daughters drift by lexicon only)

// ============================================================ small parsing helpers

int parseInt(std::string_view v, int def) {
	size_t i = 0;
	bool neg = false;
	if (i < v.size() && v[i] == '-') { neg = true; i++; }
	if (i >= v.size()) return def;
	long x = 0;
	for (; i < v.size(); i++) {
		const char c = v[i];
		if (c < '0' || c > '9') return def;
		x = x * 10 + (c - '0');
		if (x > 100000000L) return def;
	}
	return int(neg ? -x : x);
}

template <class Fn> void eachPiece(std::string_view v, char sep, Fn&& fn) {
	size_t start = 0;
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i] == sep) {
			fn(v.substr(start, i - start));
			start = i + 1;
		}
	}
	fn(v.substr(start));
}

struct Tok {
	std::string s;          // lower case letters, or A O U placeholders, or "'" ; "" = none (endings only)
	int w = 1;              // weight >= 1
	int r = 0;              // residue of its letters
};

// Accepts lower-case letters, the harmony placeholders A O U, and nothing else.
bool tokenTextOk(std::string_view s) {
	for (char c : s) {
		if (!((c >= 'a' && c <= 'z') || c == 'A' || c == 'O' || c == 'U')) return false;
	}
	return s.size() <= 8;
}

// "th:2,k:3,m" -> tokens; "_" is the empty token (allowed only when allowEmpty).
void parseTokens(std::string_view v, std::vector<Tok>& out, bool allowEmpty = false) {
	out.reserve(out.size() + size_t(std::count(v.begin(), v.end(), ',')) + 1);
	eachPiece(v, ',', [&out, allowEmpty](std::string_view item) {
		if (item.empty()) return;
		const size_t colon = item.find(':');
		std::string_view s = item.substr(0, colon);
		if (s == "_") {
			if (!allowEmpty) return;
			s = std::string_view();
		} else if (s.empty() || !tokenTextOk(s)) {
			return;
		}
		Tok t;
		t.s.assign(s);
		t.w = colon == std::string_view::npos ? 1 : std::clamp(parseInt(item.substr(colon + 1), 1), 1, 99);
		t.r = residOf(t.s);
		out.push_back(std::move(t));
	});
}

void parseStrings(std::string_view v, std::vector<std::string>& out) {
	out.reserve(out.size() + size_t(std::count(v.begin(), v.end(), ',')) + 1);
	eachPiece(v, ',', [&out](std::string_view item) {
		if (!item.empty() && tokenTextOk(item)) out.emplace_back(item);
	});
}

void parseIntList(std::string_view v, std::vector<int>& out) {
	eachPiece(v, ',', [&out](std::string_view item) {
		if (item.empty()) return;
		const int x = parseInt(item, -9999);
		if (x != -9999) out.push_back(x);
	});
}

// ============================================================ a parsed language

struct Lang {
	int fam = 0, gen = 0, contact = -1;
	std::vector<int> laws;
	std::vector<Tok> on, om, vo, fr, bk, lo, cm, cf, pends;
	int pe0 = 0, pe1 = 0, pcm = 0, pcf = 0, pl = 0, pg = 0;
	std::string gem;
	int syl[3] = { 1, 1, 1 };
	int form[6] = { 1, 1, 0, 0, 0, 0 };   // gen, bare, two, hyph, redup, pre
	int pRoot = 0;
	std::vector<std::string> roots, gens, pres, titles, ships;
	std::string god;
	const FamilyDef& F() const { return kFam[fam]; }
};

// ============================================================ acceptance: pronounceable, clean

// Letters, spaces, hyphens and apostrophes only. Consonant runs of 4+ and runs of 3+ identical
// letters are refused, and so are 3+ vowels in a row (y is a consonant). Each separator sits between
// two letters.
bool pronounceable(std::string_view s) {
	if (s.empty()) return false;
	int cons = 0, vow = 0, same = 0;
	char prev = 0;
	for (size_t i = 0; i < s.size(); i++) {
		const char raw = s[i];
		if (isSep(raw)) {
			if (i == 0 || i + 1 >= s.size() || !isLetterCh(s[i - 1]) || !isLetterCh(s[i + 1])) return false;
			cons = 0;
			vow = 0;
			same = 0;
			prev = 0;
			continue;
		}
		if (!isLetterCh(raw)) return false;
		const char c = lowerCh(raw);
		same = (c == prev) ? same + 1 : 1;
		if (same >= 3) return false;
		prev = c;
		if (isVowelCh(c)) {
			vow++;
			cons = 0;
			if (vow >= 3) return false;
		} else {
			cons++;
			vow = 0;
			if (cons >= 4) return false;
		}
	}
	return true;
}

// The blocklist (docs/content/cultures.md, "Blocklist"). Fragments are refused anywhere in the letters of
// a name; whole words are refused as a whole name or as one word of it. Profanity and slurs in English and
// in the languages whose sounds the families echo, real deities, real places and common real words that
// these sound systems can produce, and famous invented names (no imitation of other worlds).
constexpr std::string_view kFrag[] = {
	"fuck", "fuk", "shit", "cunt", "cock", "dick", "piss", "slut", "whore", "bitch", "twat", "wank", "nigg", "nigr",
	"negr", "fagg", "rape", "anus", "arse", "penis", "vagin", "porn", "bugger", "bollock", "bastard", "pussy", "dildo",
	"tosser", "boob", "retard", "prick", "jizz", "kike", "chink", "gook", "coon", "kaffir", "kafir", "sharmut", "charmut",
	"puta", "merd", "cazz", "stronz", "vittu", "perkel", "kurwa", "pizd", "blyad", "mudak", "pidor", "zhopa", "govno",
	"chutiy", "bhench", "madarch", "mboro", "msenge", "matako", "manko", "sibal", "shibal", "caralh", "malaka", "kontol",
	"kaka", "caca", "kanaka", "darkie", "golliwog", "squaw", "redskin", "wetback", "hitler", "satan", "lucifer",
	"mordor", "gondor", "sauron", "khazad", "valinor", "numenor", "rivendell", "isengard", "westeros", "dothrak",
	"valyria", "tatooine", "arrakis", "azeroth", "tamriel", "skyrim", "hogwart", "narnia", "hyrule", "hobbit", "smaug",
	"frodo", "gandalf", "aragorn", "legolas", "galadriel", "morrowind", "lannister",
};

constexpr std::string_view kWhole[] = {
	// profanity, slurs, bodily words
	"ass", "cum", "tit", "fag", "poo", "pee", "spic", "wop", "dyke", "damn", "hell", "crap", "sex", "homo", "butt",
	"shag", "turd", "nazi", "queer", "poop", "suka", "khuy", "huy", "hui", "kus", "kuss", "kos", "kir", "zibb", "ayr",
	"khara", "kalb", "gandu", "randi", "lund", "kuma", "malaya", "shoga", "baka", "kuso", "unko", "shabi", "verga",
	"polla", "porra", "bosta", "foda", "gamo", "skata", "siktir", "sik", "pula", "muie", "puki", "titi", "memek",
	"pantat", "babi", "pupu", "popo", "pipi", "sambo", "bimbo", "honky", "haole", "wog", "dago", "yid", "heeb",
	"kraut", "spaz", "mong", "abo", "lubra", "jap", "paki", "troia", "culo", "puto", "merda", "kuk", "fitta", "hora",
	"negro", "gringo", "injun", "beaner", "coolie", "boong", "tranny", "paska", "kusi", "huora", "faen", "chuj",
	"jebac", "ebat", "mavi", "teke", "tutae", "polack", "pikey", "gyppo", "dong", "bonk", "knob", "bum", "dung", "wank",
	"isis",
	// real deities and holy names
	"zeus", "hera", "apollo", "athena", "ares", "hades", "osiris", "amun", "amon", "thor", "odin", "loki", "freya",
	"frigg", "baal", "allah", "jesus", "christ", "buddha", "vishnu", "shiva", "brahma", "kali", "ganesh", "krishna",
	"rama", "yahweh", "jehovah", "elohim", "marduk", "ishtar", "inanna", "anubis", "horus", "seth", "pele", "maui",
	"tane", "kane", "lono", "olorun", "shango", "ogun", "oshun", "ukko", "tapio", "perun", "veles", "svarog", "dagda",
	"lugh", "brigid", "freyr", "baldr", "tyr", "heimdall", "juno", "minerva", "neptune", "vulcan", "janus", "diana",
	"ceres", "bacchus", "dionysus", "hermes", "poseidon", "gaia", "kronos", "cronus", "rhea", "tengri", "nzambi",
	"mulungu", "ngai", "akua", "mithra", "ahura", "atum", "aten", "ptah", "thoth", "isis", "sobek", "tiamat", "enlil",
	"anu", "ilmatar", "ahti", "kuu", "hine", "tangaroa", "rangi", "papa", "tiki", "maya", "agni", "indra", "surya",
	// real places these sounds can make
	"rome", "roma", "paris", "lima", "milan", "kenya", "mali", "oman", "iran", "iraq", "taiwan", "kobe", "osaka",
	"toledo", "malaga", "sofia", "riga", "oslo", "malta", "hanoi", "lagos", "dakar", "napoli", "milano", "verona",
	"siena", "pisa", "torino", "genoa", "venezia", "messina", "palermo", "ravenna", "sorrento", "amalfi", "salerno",
	"modena", "parma", "padova", "trento", "cremona", "lucca", "ancona", "rimini", "perugia", "arezzo", "livorno",
	"taranto", "lecce", "catania", "como", "asti", "alba", "imola", "faenza", "cesena", "savona", "vicenza", "treviso",
	"qatar", "dubai", "baghdad", "basra", "riyadh", "medina", "mecca", "doha", "sanaa", "aden", "shiraz", "tabriz",
	"herat", "kabul", "tunis", "tripoli", "rabat", "fez", "agadir", "zahir", "zarqa", "hama", "homs", "aqaba",
	"kinshasa", "luanda", "lusaka", "harare", "malawi", "zambia", "ndola", "mbabane", "nairobi", "mombasa", "kampala",
	"kigali", "dodoma", "arusha", "zanzibar", "kisumu", "ngozi", "lilongwe", "maputo", "congo", "kongo", "bongo",
	"mongo", "rwanda", "burundi", "uganda", "tonga", "samoa", "fiji", "tahiti", "oahu", "kona", "hilo", "hawaii",
	"lanai", "kauai", "molokai", "honolulu", "waikiki", "bergen", "narvik", "tromso", "bodo", "malmo", "uppsala",
	"visby", "odense", "aarhus", "skagen", "thule", "oulu", "turku", "tampere", "espoo", "lahti", "kotka", "pori",
	"kuopio", "joensuu", "vaasa", "kemi", "salo", "athens", "sparta", "corinth", "delphi", "thebes", "argos", "patras",
	"rhodes", "naxos", "paros", "samos", "lesbos", "ithaca", "troy", "pylos", "thera", "crete", "kos", "troas", "smyrna",
	"tyre", "sidon", "petra", "babylon", "nineveh", "sumer", "akkad", "carthage", "memphis", "luxor", "giza", "cairo",
	"ganda", "tara", "erin", "eire", "india", "china", "japan", "korea", "russia", "spain", "italia", "texas", "dallas",
	"dakota", "alaska", "nevada", "kansas", "orlando", "atlanta", "toronto", "london", "berlin", "madrid", "lisbon",
	"vienna", "prague", "warsaw", "kiev", "moscow", "tokyo", "seoul", "delhi", "mumbai", "dhaka", "lhasa", "tibet",
	"mars", "venus", "terra", "earth", "luna", "pluto", "titan",
	// common real words
	"banana", "salami", "lasagna", "panama", "piano", "solo", "pasta", "pizza", "sonata", "opera", "arena", "area",
	"aroma", "drama", "lava", "mama", "nana", "lane", "line", "lime", "lemon", "melon", "salon", "talon", "salsa",
	"samba", "tango", "rumba", "mango", "llama", "camera", "cinema", "sofa", "toga", "tuba", "viola", "villa",
	"vanilla", "gorilla", "umbrella", "sierra", "aria", "tiara", "mantra", "tundra", "dilemma", "comma", "manna",
	"sauna", "fauna", "flora", "nova", "diva", "viva", "visa", "pita", "rota", "lotto", "motto", "ditto", "veto",
	"memo", "demo", "radio", "patio", "ratio", "rodeo", "video", "volcano", "tornado", "potato", "tomato", "avocado",
	"torpedo", "domino", "casino", "vino", "lido", "libido", "ego", "logo", "sushi", "sake", "tofu", "karma", "kiwi",
	"wiki", "taboo", "tapu", "aloha", "mahalo", "moana", "nui", "wai", "motu", "kai", "hula", "lei", "luau", "ukulele",
	"safari", "simba", "jambo", "hakuna", "matata", "tembo", "dear", "deer", "door", "dog", "god", "rod", "rug", "drug",
	"drum", "bud", "bug", "gut", "rot", "grog", "grub", "druid", "gong", "dork", "dud", "dumb", "dunk", "gunk", "junk",
	"hunk", "punk", "rank", "tank", "bank", "dank", "honk", "gob", "orc", "ork", "elf", "troll", "dragon", "dwarf",
	"goblin", "endor", "rohan", "moria", "hoth", "naboo", "sith", "jedi", "dune", "kirk", "spock", "data", "borg",
	"lord", "king", "queen", "duke", "dame", "lady", "saint", "sir", "tsar", "czar", "shah", "khan", "emir", "sultan",
	"pasha", "bey", "raja", "rajah", "jarl", "earl", "archon", "basileus", "kaiser", "caesar", "pharaoh", "ariki",
	"mwami", "kabaka", "inca", "tina", "lisa", "nina", "dina", "mona", "rita", "sara", "anna", "maria", "elena",
	"olga", "ivan", "boris", "igor", "oleg", "hans", "lars", "sven", "erik", "olaf", "knut", "ingrid", "astrid",
	"helga", "ali", "omar", "amir", "karim", "rashid", "hassan", "fatima", "aisha", "layla", "zara", "yusuf", "kevin",
	"brian", "susan", "karen", "linda", "laura", "diane", "denise", "teresa", "sandra", "monica", "angela", "pamela",
	"tamara", "marina", "mario", "romeo", "julia", "bella", "stella", "luna", "nora", "dora", "vera", "rosa", "lara",
	"mara", "kara", "sasha", "tanya", "kate", "mike", "dave", "steve", "tom", "tim", "bob", "ron", "don", "dan", "ken",
	"mark", "luke", "john", "paul", "peter", "james", "noah", "adam", "eve", "abel", "cain", "moses", "aaron", "david",
	"solomon", "sheba", "judas", "pilate", "herod", "nero", "plato", "homer", "virgil", "dante", "tolkien",
	"hamlet", "tunic", "toga", "sandal", "kebab", "falafel", "hummus", "tahini", "harem", "hijab", "burka", "fatwa",
	"jihad", "imam", "mullah", "sheikh", "bazaar", "oasis", "sahara", "nomad", "camel", "henna", "sofa", "kismet",
	"mosque", "minaret", "islam", "quran", "koran", "torah", "bible", "amen", "halo", "hallo", "hello", "holla",
	"salve", "ciao", "basta", "mafia", "omerta", "pronto", "finale", "allegro", "forte", "tempo", "presto", "vista",
	"fiesta", "siesta", "plaza", "bodega", "mesa", "pueblo", "rancho", "macho", "nacho", "taco", "burrito", "tequila",
	"tortilla", "paella", "chorizo", "tapas", "salsa", "mojo", "voodoo", "zombie", "juju", "obeah", "mumbo", "jumbo",
	"bongo", "conga", "limbo", "mambo", "banjo", "zulu", "bantu", "masai", "maasai", "swahili", "hindi", "urdu",
	"thai", "lao", "malay", "tamil", "sikh", "hindu", "inuit", "sami", "lapp", "finn", "dane", "swede", "norse",
	"viking", "saga", "edda", "rune", "skald", "fjord", "troll", "valhalla", "ragnarok", "midgard", "asgard",
	"sauna", "sisu", "kalevala", "kantele", "moomin", "nokia", "ikea", "lego", "volvo", "saab", "skoda", "lada",
	"fiat", "lancia", "honda", "toyota", "mazda", "subaru", "suzuki", "nissan", "kia", "audi", "opel", "seat",
	"tesla", "nike", "adidas", "puma", "rolex", "sony", "pepsi", "fanta", "sprite", "tide", "omo", "dove",
	"loro", "toro", "oro", "mar", "sol", "luz", "dios", "deus", "dei", "theos", "logos", "polis", "agora", "acropolis",
	"nemesis", "hubris", "chaos", "kosmos", "cosmos", "eros", "thanatos", "psyche", "echo", "iris", "nike", "tyche",
	"atlas", "titan", "kraken", "hydra", "medusa", "gorgon", "sphinx", "pegasus", "phoenix", "satyr", "nymph", "siren",
	"cyclops", "minotaur", "centaur", "harpy", "chimera", "cerberus", "charon", "styx", "lethe", "elysium", "tartarus",
	"olympus", "athos", "ida", "ossa", "pelion", "parnassus", "helicon", "arcadia", "utopia", "eden", "zion", "sinai",
	"golgotha", "jericho", "sodom", "gomorrah", "babel", "ophir", "tarshish", "atlantis", "lemuria", "avalon",
	"camelot", "eldorado", "shangri", "xanadu", "lilliput",
};

// The lists grouped by first letter at compile time (a counting sort), so a lookup scans one letter's group.
template <size_t N> struct Buckets {
	std::array<std::string_view, N> v{};
	std::array<uint16_t, 27> start{};
};
template <size_t N> constexpr Buckets<N> bucketize(const std::string_view (&a)[N]) {
	Buckets<N> b{};
	std::array<uint16_t, 26> count{};
	for (const std::string_view x : a) count[size_t(x[0] - 'a')]++;
	uint16_t acc = 0;
	for (size_t c = 0; c < 26; c++) { b.start[c] = acc; acc = uint16_t(acc + count[c]); }
	b.start[26] = acc;
	std::array<uint16_t, 26> pos{};
	for (size_t c = 0; c < 26; c++) pos[c] = b.start[c];
	for (const std::string_view x : a) b.v[pos[size_t(x[0] - 'a')]++] = x;
	return b;
}
constexpr auto kWholeB = bucketize(kWhole);
constexpr auto kFragB = bucketize(kFrag);

bool isWholeBlocked(std::string_view w) {
	if (w.empty() || w[0] < 'a' || w[0] > 'z') return false;
	const size_t c = size_t(w[0] - 'a');
	for (size_t i = kWholeB.start[c]; i < kWholeB.start[c + 1]; i++) {
		if (kWholeB.v[i].size() == w.size() && kWholeB.v[i] == w) return true;
	}
	return false;
}

bool fragmentIn(std::string_view flat) {
	for (size_t i = 0; i < flat.size(); i++) {
		const char ch = flat[i];
		if (ch < 'a' || ch > 'z') continue;
		const size_t c = size_t(ch - 'a');
		for (size_t k = kFragB.start[c]; k < kFragB.start[c + 1]; k++) {
			const std::string_view f = kFragB.v[k];
			if (f.size() <= flat.size() - i && flat.compare(i, f.size(), f) == 0) return true;
		}
	}
	return false;
}

// Whole words are checked as the words of the name, as the pieces between glottal stops ("Nazi'ushta"),
// and as the whole name with its separators dropped; fragments anywhere in its letters.
bool blocked(std::string_view s) {
	std::string flat, word, piece;
	auto endPiece = [&]() -> bool { const bool hit = isWholeBlocked(piece); piece.clear(); return hit; };
	auto endWord = [&]() -> bool { const bool hit = isWholeBlocked(word); word.clear(); return hit; };
	for (char c : s) {
		if (isLetterCh(c)) {
			const char l = lowerCh(c);
			flat += l;
			word += l;
			piece += l;
		} else if (c == '\'') {
			if (endPiece()) return true;
		} else {
			if (endPiece() || endWord()) return true;
		}
	}
	if (endPiece() || endWord() || isWholeBlocked(flat)) return true;
	return fragmentIn(flat);
}

// Sequences no family may write: English-looking word endings ("Midelton", "Skifenman") and long vowels
// pressed against a glottal stop.
const char* const kForbidAll = "ton$,ham$,wick$,ford$,ville$,burg$,shire$,field$,land$,ing$,son$,man$,ley$,by$,ster$,ness$,hold$,jet$,^iron,aa',ii',uu','aa,'ii,'uu";

// Is `piece` in `low`? A leading ^ ties it to the start of a word, a trailing $ to the end of one.
bool pieceIn(std::string_view low, std::string_view piece) {
	const bool atStart = !piece.empty() && piece.front() == '^';
	const bool atEnd = !piece.empty() && piece.back() == '$';
	if (atStart) piece.remove_prefix(1);
	if (atEnd) piece.remove_suffix(1);
	if (piece.empty()) return false;
	if (!atStart && !atEnd) return low.find(piece) != std::string_view::npos;
	size_t from = 0;
	while (from <= low.size()) {
		size_t to = from;
		while (to < low.size() && low[to] != ' ' && low[to] != '-') to++;
		const std::string_view w = low.substr(from, to - from);
		if (w.size() >= piece.size()) {
			const bool okStart = !atStart || w.substr(0, piece.size()) == piece;
			const bool okEnd = !atEnd || w.substr(w.size() - piece.size()) == piece;
			if (okStart && okEnd && (atStart || atEnd)) {
				if (atStart && atEnd) { if (w == piece) return true; }
				else return true;
			}
		}
		from = to + 1;
	}
	return false;
}

// Family-forbidden sequences, checked on the lower-case text (separators break a sequence).
bool forbiddenIn(std::string_view low, const FamilyDef& F) {
	bool hit = false;
	std::array<uint64_t, 11> pairs{};   // the name's letter pairs, 26 x 26 bits
	bool vv = false;
	for (size_t i = 0; i + 1 < low.size(); i++) {
		const char a = low[i], b = low[i + 1];
		if (a < 'a' || a > 'z' || b < 'a' || b > 'z') continue;
		const size_t bit = size_t(a - 'a') * 26 + size_t(b - 'a');
		pairs[bit >> 6] |= uint64_t(1) << (bit & 63);
		vv = vv || (isVowelCh(a) && isVowelCh(b));
	}
	auto scan = [&](std::string_view piece) {
		if (hit || piece.empty()) return;
		if (piece == "VV") { hit = vv; return; }
		if (piece.size() == 2 && piece[0] >= 'a' && piece[0] <= 'z' && piece[1] >= 'a' && piece[1] <= 'z') {
			const size_t bit = size_t(piece[0] - 'a') * 26 + size_t(piece[1] - 'a');
			hit = (pairs[bit >> 6] >> (bit & 63)) & 1u;
			return;
		}
		if (piece.size() >= 3 && piece[0] != '^' && piece.back() != '$' && piece[1] >= 'a' && piece[0] >= 'a') {
			// a longer piece can only be there when its first pair is
			const size_t bit = size_t(piece[0] - 'a') * 26 + size_t(piece[1] - 'a');
			if (piece[1] <= 'z' && piece[0] <= 'z' && !((pairs[bit >> 6] >> (bit & 63)) & 1u)) return;
		}
		if (pieceIn(low, piece)) hit = true;
	};
	eachPiece(F.forbid, ',', scan);
	eachPiece(kForbidAll, ',', scan);
	return hit;
}

std::string lowerStr(std::string_view s) {
	std::string r(s);
	for (char& c : r) c = lowerCh(c);
	return r;
}

// Euphony, for every family: no stutter (the same two or three letters twice in a row, "rara", "nene",
// "ulul"), except that a family that reduplicates may open a word with one ("Lelemoa"); at most one glottal
// stop in a word and never between two equal vowels ("u'u"); each word at most `maxWord` letters.
bool euphonious(std::string_view s, const FamilyDef& F, int maxWord) {
	std::string w;
	int apos = 0;
	auto wordFine = [&]() -> bool {
		const size_t n = w.size();
		if (int(n) > maxWord) return false;
		for (size_t k = 2; k <= 3; k++) {
			for (size_t i = 0; i + 2 * k <= n; i++) {
				if (w.compare(i, k, w, i + k, k) != 0) continue;
				if (k == 2 && i == 0 && F.fRedup > 0) continue;
				return false;
			}
		}
		return true;
	};
	for (size_t i = 0; i < s.size(); i++) {
		const char c = s[i];
		if (c == '\'') {
			apos++;
			if (apos > 1) return false;
			if (i > 0 && i + 1 < s.size() && lowerCh(s[i - 1]) == lowerCh(s[i + 1])) return false;
			continue;
		}
		if (c == ' ' || c == '-') {
			if (!wordFine()) return false;
			w.clear();
			apos = 0;
			continue;
		}
		w += lowerCh(c);
	}
	return wordFine();
}

// A name that may be returned: length, sounds, the family's forbidden sequences, euphony, the blocklist,
// never the god's own name, and (coded names) the spelling code.
bool acceptable(std::string_view s, const Lang& L, int lo, int hi, bool coded, int maxWord = 99) {
	const int n = int(s.size());
	if (n < lo || n > hi) return false;
	if (!pronounceable(s)) return false;
	if (!euphonious(s, L.F(), maxWord)) return false;
	const std::string low = lowerStr(s);
	if (forbiddenIn(low, L.F())) return false;
	if (blocked(s)) return false;
	if (coded && !codeOk(s)) return false;
	return lettersLower(s) != L.god;
}

// A stored word (root, ending, god, title): pronounceable, clean, allowed by the family.
bool wordOk(std::string_view s, const FamilyDef& F, size_t lo, size_t hi) {
	if (s.size() < lo || s.size() > hi) return false;
	std::string plain;
	for (char c : s) plain += (c == 'A' ? 'a' : c == 'O' ? 'o' : c == 'U' ? 'u' : c);
	if (!pronounceable(plain)) return false;
	if (forbiddenIn(plain, F)) return false;
	return !blocked(plain);
}

// ============================================================ the record: write and parse

void putInt(std::string& o, const char* key, int x) {
	o += ';';
	o += key;
	o += '=';
	o += std::to_string(x);
}
void putInts(std::string& o, const char* key, const int* v, int n) {
	o += ';';
	o += key;
	o += '=';
	for (int i = 0; i < n; i++) {
		if (i > 0) o += ',';
		o += std::to_string(v[i]);
	}
}
void putTokens(std::string& o, const char* key, const std::vector<Tok>& v) {
	o += ';';
	o += key;
	o += '=';
	for (size_t i = 0; i < v.size(); i++) {
		if (i > 0) o += ',';
		o += v[i].s.empty() ? std::string("_") : v[i].s;
		o += ':';
		o += std::to_string(v[i].w);
	}
}
void putStrings(std::string& o, const char* key, const std::vector<std::string>& v) {
	o += ';';
	o += key;
	o += '=';
	for (size_t i = 0; i < v.size(); i++) {
		if (i > 0) o += ',';
		o += v[i];
	}
}

std::string writeLang(const Lang& L) {
	std::string o = "dm2";
	putInt(o, "f", L.fam);
	putInt(o, "g", L.gen);
	putInt(o, "c", L.contact);
	putInts(o, "L", L.laws.data(), int(L.laws.size()));
	putTokens(o, "on", L.on);
	putTokens(o, "om", L.om);
	putTokens(o, "vo", L.vo);
	putTokens(o, "fr", L.fr);
	putTokens(o, "bk", L.bk);
	putTokens(o, "lo", L.lo);
	putTokens(o, "cm", L.cm);
	putTokens(o, "cf", L.cf);
	const int p[6] = { L.pe0, L.pe1, L.pcm, L.pcf, L.pl, L.pg };
	putInts(o, "p", p, 6);
	o += ";gm=" + L.gem;
	putInts(o, "sy", L.syl, 3);
	putInts(o, "fw", L.form, 6);
	putInt(o, "pr", L.pRoot);
	putStrings(o, "ro", L.roots);
	putStrings(o, "gn", L.gens);
	putStrings(o, "pp", L.pres);
	putTokens(o, "qn", L.pends);
	putStrings(o, "rt", L.titles);
	putStrings(o, "sw", L.ships);
	o += ";gd=" + L.god;
	return o;
}

void mergeTokens(std::vector<Tok>& v) {
	std::vector<Tok> out;
	for (Tok& t : v) {
		bool merged = false;
		for (Tok& o : out) {
			if (o.s == t.s) { o.w = std::min(99, o.w + t.w); merged = true; break; }
		}
		if (!merged) {
			t.r = residOf(t.s);
			out.push_back(std::move(t));
		}
	}
	v = std::move(out);
}

std::string makeGodFallback(const Lang& L);

// Defaults for anything missing, and clamps so that no record can break the generators.
void finalize(Lang& L, bool deep) {
	const FamilyDef& F = L.F();
	if (L.on.empty()) parseTokens(F.on, L.on);
	if (L.om.empty()) parseTokens(F.om, L.om);
	if (F.harmony) {
		if (L.fr.empty()) parseTokens(F.fr, L.fr);
		if (L.bk.empty()) parseTokens(F.bk, L.bk);
	} else if (L.vo.empty()) {
		parseTokens(F.vo, L.vo);
	}
	// vowels must be vowels; onsets and codas consonants (in place: parsing runs on every call)
	auto keep = [](std::vector<Tok>& v, bool vowels) {
		v.erase(std::remove_if(v.begin(), v.end(), [vowels](const Tok& t) {
			if (t.s.empty()) return true;
			for (char c : t.s) if (isVowelCh(c) != vowels || c < 'a' || c > 'z') return true;
			return false;
		}), v.end());
	};
	keep(L.on, false);
	keep(L.om, false);
	keep(L.cm, false);
	keep(L.cf, false);
	keep(L.vo, true);
	keep(L.fr, true);
	keep(L.bk, true);
	keep(L.lo, true);
	if (L.on.empty()) parseTokens("l:3,m:2,n:3,r:3,s:2,t:2", L.on);
	if (L.om.empty()) parseTokens("l:3,m:2,n:3,r:3,s:2,t:2", L.om);
	if (F.harmony) {
		if (L.fr.empty()) parseTokens("e:2,i:2", L.fr);
		if (L.bk.empty()) parseTokens("a:2,o:2,u:2", L.bk);
	} else if (L.vo.empty()) {
		parseTokens("a:3,e:3,i:3,o:3", L.vo);
	}
	if (deep) {
		mergeTokens(L.on);
		mergeTokens(L.om);
		mergeTokens(L.cm);
		mergeTokens(L.cf);
	}
	L.pe0 = std::clamp(L.pe0, 0, 600);
	L.pe1 = std::clamp(L.pe1, 0, 600);
	L.pcm = std::clamp(L.pcm, 0, 1000);
	L.pcf = std::clamp(L.pcf, 0, 1000);
	L.pl = std::clamp(L.pl, 0, 800);
	L.pg = std::clamp(L.pg, 0, 800);
	int sylSum = 0;
	for (int& s : L.syl) { s = std::clamp(s, 0, 99); sylSum += s; }
	if (sylSum == 0) { L.syl[0] = 1; L.syl[1] = 3; L.syl[2] = 1; }
	int formSum = 0;
	for (int& f : L.form) { f = std::clamp(f, 0, 999); formSum += f; }
	if (formSum == 0) { L.form[0] = 1; L.form[1] = 1; }
	L.pRoot = std::clamp(L.pRoot, 0, 600);
	std::string gem;
	for (char c : L.gem) {
		if (c >= 'a' && c <= 'z' && !isVowelCh(c)) gem += c;
	}
	L.gem = gem;
	// stored words: lower case (generics may keep harmony placeholders) and sound
	// (a parsed record only gets the cheap checks: every name built from these words is validated anyway)
	auto clean = [&F, deep](std::vector<std::string>& v, size_t lo, size_t hi, size_t cap) {
		v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& s) {
			return deep ? !wordOk(s, F, lo, hi) : (s.size() < lo || s.size() > hi);
		}), v.end());
		if (v.size() > cap) v.resize(cap);
	};
	clean(L.roots, 2, 8, 32);
	clean(L.gens, 2, 7, 24);
	clean(L.pres, 1, 7, 8);
	clean(L.titles, 3, 10, 4);
	clean(L.ships, 3, 9, 5);
	if (L.gens.empty()) parseStrings(F.gens, L.gens);
	if (L.titles.empty()) parseStrings(F.titles, L.titles);
	if (L.ships.empty()) parseStrings(F.ships, L.ships);
	L.pends.erase(std::remove_if(L.pends.begin(), L.pends.end(), [&](const Tok& t) {
		return t.s.empty() || (deep ? !wordOk(t.s, F, 1, 5) : t.s.size() > 5);
	}), L.pends.end());
	// the god's word: fully checked when a language is built; a parsed record gets the cheap checks here and
	// god() checks the rest before it returns the name
	bool godLetters = L.god.size() >= 3 && L.god.size() <= size_t(kNamesTune.maxLen);
	for (char c : L.god) godLetters = godLetters && c >= 'a' && c <= 'z';
	if (!godLetters || !pronounceable(L.god) || (deep && !wordOk(L.god, F, 3, size_t(kNamesTune.maxLen)))) L.god = makeGodFallback(L);
}

// dm2 records only; false (and a default language of family 0) otherwise.
bool parseDm2(std::string_view text, Lang& L) {
	L = Lang();
	if (text.size() < 4 || text.substr(0, 4) != "dm2;") return false;
	bool famOk = false;
	eachPiece(text.substr(4), ';', [&L, &famOk](std::string_view field) {
		const size_t eq = field.find('=');
		if (eq == std::string_view::npos) return;
		const std::string_view key = field.substr(0, eq);
		const std::string_view val = field.substr(eq + 1);
		if (key == "f") {
			const int f = parseInt(val, -1);
			if (f >= 0 && f < kFamilies) { L.fam = f; famOk = true; }
		}
		else if (key == "g") L.gen = std::clamp(parseInt(val, 0), 0, 9999);
		else if (key == "c") L.contact = std::clamp(parseInt(val, -1), -1, kFamilies - 1);
		else if (key == "L") parseIntList(val, L.laws);
		else if (key == "on") parseTokens(val, L.on);
		else if (key == "om") parseTokens(val, L.om);
		else if (key == "vo") parseTokens(val, L.vo);
		else if (key == "fr") parseTokens(val, L.fr);
		else if (key == "bk") parseTokens(val, L.bk);
		else if (key == "lo") parseTokens(val, L.lo);
		else if (key == "cm") parseTokens(val, L.cm);
		else if (key == "cf") parseTokens(val, L.cf);
		else if (key == "p") {
			std::vector<int> v;
			parseIntList(val, v);
			int* dst[6] = { &L.pe0, &L.pe1, &L.pcm, &L.pcf, &L.pl, &L.pg };
			for (size_t i = 0; i < 6 && i < v.size(); i++) *dst[i] = v[i];
		}
		else if (key == "gm") L.gem.assign(val);
		else if (key == "sy") {
			std::vector<int> v;
			parseIntList(val, v);
			for (size_t i = 0; i < 3 && i < v.size(); i++) L.syl[i] = v[i];
		}
		else if (key == "fw") {
			std::vector<int> v;
			parseIntList(val, v);
			for (size_t i = 0; i < 6 && i < v.size(); i++) L.form[i] = v[i];
		}
		else if (key == "pr") L.pRoot = parseInt(val, 0);
		else if (key == "ro") parseStrings(val, L.roots);
		else if (key == "gn") parseStrings(val, L.gens);
		else if (key == "pp") parseStrings(val, L.pres);
		else if (key == "qn") parseTokens(val, L.pends);
		else if (key == "rt") parseStrings(val, L.titles);
		else if (key == "sw") parseStrings(val, L.ships);
		else if (key == "gd") L.god.assign(val);
	});
	if (!famOk) {
		L = Lang();
		return false;
	}
	std::vector<int> laws;
	for (int id : L.laws) {
		if (id >= 0 && id < 64 && int(laws.size()) < kMaxLaws) laws.push_back(id);
	}
	L.laws = laws;
	finalize(L, false);
	return true;
}

// ============================================================ generation primitives

template <class T, class WFn> int pickWeighted(const std::vector<T>& v, Rng& r, WFn&& wfn) {
	return r.weightedIndex(int(v.size()), [&](int k) { return wfn(v[size_t(k)]); });
}

const Tok* pickTok(const std::vector<Tok>& v, Rng& r) {
	const int i = pickWeighted(v, r, [](const Tok& t) { return double(t.w); });
	return i < 0 ? nullptr : &v[size_t(i)];
}

// Harmony class of a text: 2 (back) when it has a, o or u, else 1 (front) when it has e or i, else 0.
int classOf(std::string_view s) {
	bool back = false, front = false;
	for (char c : s) {
		const char l = lowerCh(c);
		if (l == 'a' || l == 'o' || l == 'u') back = true;
		if (l == 'e' || l == 'i') front = true;
	}
	return back ? 2 : (front ? 1 : 0);
}

// Does a vowel token fit the harmony class? cls 0: anything; 1 (front): no a o u; 2 (back): no e, and
// some a o u (plain i is neutral and fits too).
bool fitsClass(std::string_view s, int cls) {
	if (cls == 0) return true;
	bool back = false, e = false;
	for (char c : s) {
		if (c == 'a' || c == 'o' || c == 'u') back = true;
		if (c == 'e') e = true;
	}
	if (cls == 1) return !back;
	return !e && (back || s == "i");
}

// Harmonic placeholders: A -> a | e, O -> o | e, U -> u | i (back | front).
std::string resolveHarm(std::string_view s, int cls) {
	std::string out;
	for (char c : s) {
		if (c == 'A') out += (cls == 1 ? 'e' : 'a');
		else if (c == 'O') out += (cls == 1 ? 'e' : 'o');
		else if (c == 'U') out += (cls == 1 ? 'i' : 'u');
		else out += c;
	}
	return out;
}

int startClass(const Lang& L, Rng& r) {
	if (!L.F().harmony) return 0;
	return r.chance(0.45) ? 1 : 2;
}

const std::vector<Tok>& vowelsOf(const Lang& L, int cls) {
	if (L.F().harmony) return cls == 1 ? L.fr : L.bk;
	return L.vo;
}

// Collision entropy in bits (-log2 of the chance that two draws agree) of a weighted choice that also has a
// "none" option of probability nonePm / 1000. It measures how often two names would come out the same.
double h2(const std::vector<Tok>& v, int nonePm) {
	double sum = 0;
	for (const Tok& t : v) sum += t.w;
	const double pn = sum > 0 ? std::clamp(double(nonePm) / 1000.0, 0.0, 1.0) : 1.0;
	double s2 = pn * pn;
	for (const Tok& t : v) {
		const double p = (1.0 - pn) * double(t.w) / sum;
		s2 += p * p;
	}
	return s2 > 0 ? -std::log2(s2) : 0.0;
}

constexpr double kCodeBits = 4.755;   // log2(27): what the spelling code takes from every coded name

// Bits of a fresh part of n syllables of this language (codas between syllables counted, final codas not).
double partBits(const Lang& L, int cls, int n, bool initial) {
	const double nuc = h2(vowelsOf(L, cls), 0);
	const double coda = h2(L.cm, 1000 - L.pcm);
	double b = 0;
	for (int i = 0; i < n; i++) {
		b += (i == 0 && initial) ? h2(L.on, L.pe0) : h2(L.om, L.pe1);
		b += nuc;
		if (i < n - 1) b += coda;
	}
	return b;
}

// The fewest syllables (lo..hi) that give a coded name at least `target` bits with `extra` bits from elsewhere
// (an ending, a generic). Small sound systems get longer names; rich ones stay short.
int sylsFor(const Lang& L, int cls, double extra, double target, int lo, int hi) {
	for (int n = lo; n < hi; n++) {
		if (partBits(L, cls, n, true) + extra - kCodeBits >= target) return n;
	}
	return hi;
}

double listBits(size_t n) { return n > 1 ? std::log2(double(n)) : 0.0; }

bool vvOk(const FamilyDef& F, char a, char b) {
	if (!*F.vv) return false;
	const char pair[3] = { a, b, 0 };
	bool found = false;
	eachPiece(F.vv, ',', [&](std::string_view p) { if (p == std::string_view(pair, 2)) found = true; });
	return found;
}

bool chancePm(Rng& r, int perMille) { return perMille > 0 && r.chance(double(perMille) / 1000.0); }

std::string pickVowel(const Lang& L, Rng& r, int cls, bool stressed) {
	if (stressed && !L.lo.empty() && chancePm(r, L.pl)) {
		const int i = pickWeighted(L.lo, r, [cls](const Tok& t) { return fitsClass(t.s, cls) ? double(t.w) : 0.0; });
		if (i >= 0 && fitsClass(L.lo[size_t(i)].s, cls)) return L.lo[size_t(i)].s;
	}
	const std::vector<Tok>& v = vowelsOf(L, cls);
	const int i = pickWeighted(v, r, [cls](const Tok& t) { return fitsClass(t.s, cls) ? double(t.w) : 0.0; });
	if (i >= 0) return v[size_t(i)].s;
	return cls == 1 ? "e" : "a";
}

int stressIndex(const FamilyDef& F, int total) {
	int s = 0;
	if (F.stress == 1) s = total - 2;
	else if (F.stress == 2) s = total - 1;
	else if (F.stress == 3) s = total - 3;
	return std::clamp(s, 0, std::max(0, total - 1));
}

// Appends n random syllables to `out`. first: the first syllable starts the word (initial onsets, may be
// empty); bound: text stands before it in the same word (initial onsets, never empty). sylBase: index of
// the first appended syllable in the whole word; stressAt: the stressed syllable's index. lastEnds: the
// last appended syllable ends the word (final codas) rather than being followed by more.
void freshInto(const Lang& L, Rng& r, std::string& out, int n, bool first, bool bound, int cls, int stressAt, int sylBase, bool lastEnds) {
	const FamilyDef& F = L.F();
	bool prevStressedOpen = false;
	for (int i = 0; i < n; i++) {
		const bool initial = i == 0 && (first || bound);
		const bool stressed = sylBase + i == stressAt;
		const bool prevVowel = !out.empty() && isVowelCh(out.back());
		std::string onset;
		bool emptyMedial = false;
		if (initial) {
			if (first && !bound && chancePm(r, L.pe0)) onset.clear();
			else if (const Tok* t = pickTok(L.on, r)) onset = t->s;
		} else if (chancePm(r, L.pe1)) {
			emptyMedial = true;
		} else if (const Tok* t = pickTok(L.om, r)) {
			onset = t->s;
			if (prevStressedOpen && onset.size() == 1 && L.gem.find(onset[0]) != std::string::npos && chancePm(r, L.pg)) onset += onset;
		}
		const std::string nuc = pickVowel(L, r, cls, stressed);
		if (emptyMedial && prevVowel) {
			const bool single = out.size() < 2 || !isVowelCh(out[out.size() - 2]);
			if (F.apos) onset = "'";
			else if (!(nuc.size() == 1 && single && vvOk(F, out.back(), nuc[0]))) {
				if (const Tok* t = pickTok(L.om, r)) onset = t->s;
			}
		}
		std::string coda;
		const bool last = i == n - 1;
		if (last && lastEnds) {
			if (!L.cf.empty() && chancePm(r, L.pcf)) { if (const Tok* t = pickTok(L.cf, r)) coda = t->s; }
		} else if (!L.cm.empty() && chancePm(r, L.pcm)) {
			if (const Tok* t = pickTok(L.cm, r)) coda = t->s;
		}
		out += onset;
		out += nuc;
		out += coda;
		prevStressedOpen = stressed && coda.empty();
	}
}

// Letters of a text (separators and apostrophes do not count).
int lettersOf(std::string_view s) {
	int n = 0;
	for (char c : s) n += isLetterCh(c) ? 1 : 0;
	return n;
}

// The second spelling code, for long names: the letters' values weighted by their positions (1, 2, ...)
// sum to kTarget2 modulo 29. Together with the first code it keeps two long names of the same length from
// differing by two substitutions (that would need d1 + d2 = 0 mod 27 and i d1 + j d2 = 0 mod 29, impossible
// for 0 < |d| <= 25 and positions i != j below 29).
constexpr int kMod2 = 29;
constexpr int kTarget2 = 5;
constexpr int kLongName = 10;   // letters from which a name carries the second code

// Adds the letters of s to (sum v, position-weighted sum w); positions continue from `pos`.
void addSums(std::string_view s, int& pos, int& v, int& w) {
	for (char c : s) {
		if (!isLetterCh(c)) continue;
		pos++;
		v += letterVal(c);
		w += pos * letterVal(c);
	}
}

bool codeOk(std::string_view s) {
	if (residOf(s) != kTarget) return false;
	int pos = 0, v = 0, w = 0;
	addSums(s, pos, v, w);
	return pos < kLongName || w % kMod2 == kTarget2;
}

struct Opt {
	const std::string* s;   // nullptr = none (empty)
	double w;
	int r;                  // residue of its letters (first code)
	int len, v, wp;         // letters, letter sum, position-weighted letter sum (positions from 1)
};

Opt optOf(const std::string* s, double w) {
	Opt o{ s, w, 0, 0, 0, 0 };
	if (s) {
		int pos = 0;
		addSums(*s, pos, o.v, o.wp);
		o.len = pos;
		o.r = o.v % kMod;
	}
	return o;
}

// The free slot: the last syllable of a specific part, drawn among every onset x nucleus x coda the language
// allows, weighted as the language would draw them, keeping only those that make the whole name satisfy the
// spelling code (and, when the name reaches kLongName letters, the second code too). out: the word so far
// (appended to); lead: the name's earlier words; suffix: what follows in this word. follow: 0 the word ends
// here; 1 the suffix starts with a vowel; 2 with a consonant. Returns false when no syllable fits.
bool slotInto(const Lang& L, Rng& r, std::string& out, std::string_view lead, bool initial, bool bound, int cls, bool stressed, int follow, std::string_view suffix) {
	const FamilyDef& F = L.F();
	const int need = modN(kTarget - residOf(lead) - residOf(out) - residOf(suffix));
	int base = 0, preV = 0, preW = 0;
	addSums(lead, base, preV, preW);
	addSums(out, base, preV, preW);
	int sufLen = 0, sufV = 0, sufW = 0;
	addSums(suffix, sufLen, sufV, sufW);
	const char sufFirst = suffix.empty() ? 0 : lowerCh(suffix[0]);
	const bool prevVowel = !out.empty() && isVowelCh(out.back());
	const bool prevSingle = out.size() < 2 || !isVowelCh(out[out.size() - 2]);
	static const std::string kApos = "'";
	// onsets
	std::vector<Opt> ons;
	if (initial) {
		double sum = 0;
		for (const Tok& t : L.on) sum += t.w;
		const int pe = bound ? 0 : L.pe0;
		for (const Tok& t : L.on) ons.push_back(optOf(&t.s, double(t.w) * double(1000 - pe)));
		if (pe > 0) ons.push_back(optOf(nullptr, double(pe) * sum));
	} else {
		double sum = 0;
		for (const Tok& t : L.om) sum += t.w;
		for (const Tok& t : L.om) ons.push_back(optOf(&t.s, double(t.w) * double(1000 - L.pe1)));
		if (L.pe1 > 0) ons.push_back(optOf(nullptr, double(L.pe1) * sum));
	}
	// nuclei
	std::vector<Opt> nucs;
	const std::vector<Tok>& vs = vowelsOf(L, cls);
	double sumV = 0, sumL = 0;
	for (const Tok& t : vs) if (fitsClass(t.s, cls)) sumV += t.w;
	const bool longOk = stressed && !L.lo.empty() && L.pl > 0;
	if (longOk) for (const Tok& t : L.lo) if (fitsClass(t.s, cls)) sumL += t.w;
	for (const Tok& t : vs) {
		if (!fitsClass(t.s, cls)) continue;
		nucs.push_back(optOf(&t.s, (longOk && sumL > 0) ? double(t.w) * double(1000 - L.pl) * sumL : double(t.w)));
	}
	if (longOk && sumL > 0) {
		for (const Tok& t : L.lo) {
			if (fitsClass(t.s, cls)) nucs.push_back(optOf(&t.s, double(t.w) * double(L.pl) * sumV));
		}
	}
	// codas; the last one is "none" (its validity depends on the nucleus)
	std::vector<Opt> codas;
	double noneW = 0;
	bool noneOk = false;
	if (follow == 0) {
		double sum = 0;
		for (const Tok& t : L.cf) sum += t.w;
		for (const Tok& t : L.cf) codas.push_back(optOf(&t.s, double(t.w) * double(L.pcf)));
		noneOk = true;
		noneW = L.cf.empty() ? 1.0 : double(1000 - L.pcf) * sum;
	} else if (follow == 2) {
		double sum = 0;
		for (const Tok& t : L.cm) sum += t.w;
		for (const Tok& t : L.cm) codas.push_back(optOf(&t.s, double(t.w) * double(L.pcm)));
		noneOk = true;
		noneW = L.cm.empty() ? 1.0 : double(1000 - L.pcm) * sum;
	} else {
		// before a vowel: the syllable closes on a consonant (the suffix takes it as its onset), or, in
		// families with a glottal stop or hiatus, it may stay open
		double sum = 0;
		for (const Tok& t : L.cm) { codas.push_back(optOf(&t.s, double(t.w))); sum += t.w; }
		for (const Tok& t : L.om) {
			if (t.s.size() != 1) continue;
			bool dup = false;
			for (const Tok& c : L.cm) dup = dup || c.s == t.s;
			if (!dup) { codas.push_back(optOf(&t.s, double(t.w))); sum += t.w; }
		}
		noneOk = F.apos || *F.vv;
		noneW = F.apos ? sum * 0.15 : sum * 0.3;
	}
	const size_t noneIdx = codas.size();
	codas.push_back(optOf(nullptr, noneW));
	// coda indices grouped by residue (a counting sort over at most a few dozen codas)
	int start[kMod + 1] = {};
	for (size_t c = 0; c < noneIdx; c++) start[codas[c].r + 1]++;
	for (int k = 0; k < kMod; k++) start[k + 1] += start[k];
	std::vector<int> order(noneIdx);
	{
		int fill[kMod];
		for (int k = 0; k < kMod; k++) fill[k] = start[k];
		for (size_t c = 0; c < noneIdx; c++) order[size_t(fill[codas[c].r]++)] = int(c);
	}
	auto pairOk = [&](const Opt& o, const Opt& v) -> bool {
		if (o.s == nullptr && prevVowel) {
			if (F.apos) return true;
			return v.s->size() == 1 && prevSingle && vvOk(F, out.back(), (*v.s)[0]);
		}
		return true;
	};
	auto noneFits = [&](const Opt& v) -> bool {
		if (!noneOk) return false;
		if (follow != 1) return true;
		if (F.apos) return true;
		return v.s->size() == 1 && vvOk(F, v.s->back(), sufFirst);
	};
	// the second code for long names: the whole name's position-weighted sum with this candidate
	auto secondOk = [&](const Opt& o, const Opt& v, const Opt& c) -> bool {
		const int len = o.len + v.len + c.len;
		if (base + len + sufLen < kLongName) return true;
		const int candV = o.v + v.v + c.v;
		const int candW = o.wp + (v.wp + o.len * v.v) + (c.wp + (o.len + v.len) * c.v);
		const long long w2 = (long long)preW + (long long)base * candV + candW + sufW + (long long)(base + len) * sufV;
		return int(w2 % kMod2) == kTarget2;
	};
	struct Match { int o, v, c; double w; };
	std::vector<Match> matches;
	matches.reserve(64);
	double total = 0;
	for (size_t oi = 0; oi < ons.size(); oi++) {
		const Opt& o = ons[oi];
		for (size_t vi = 0; vi < nucs.size(); vi++) {
			const Opt& v = nucs[vi];
			if (!pairOk(o, v)) continue;
			const int needC = modN(need - o.r - v.r);
			for (int k = start[needC]; k < start[needC + 1]; k++) {
				const Opt& c = codas[size_t(order[size_t(k)])];
				if (c.w <= 0 || !secondOk(o, v, c)) continue;
				const double w = o.w * v.w * c.w;
				matches.push_back({ int(oi), int(vi), order[size_t(k)], w });
				total += w;
			}
			if (needC == 0 && noneFits(v) && noneW > 0 && secondOk(o, v, codas[noneIdx])) {
				const double w = o.w * v.w * noneW;
				matches.push_back({ int(oi), int(vi), int(noneIdx), w });
				total += w;
			}
		}
	}
	if (matches.empty() || total <= 0) return false;
	double x = r.f() * total;
	const Match* pick = &matches.back();
	for (const Match& m : matches) {
		if (x < m.w) { pick = &m; break; }
		x -= m.w;
	}
	const Opt& po = ons[size_t(pick->o)];
	const Opt& pv = nucs[size_t(pick->v)];
	const Opt& pc = codas[size_t(pick->c)];
	if (po.s == nullptr) { if (prevVowel && F.apos) out += kApos; }
	else out += *po.s;
	out += *pv.s;
	if (pc.s) out += *pc.s;
	else if (follow == 1 && F.apos) out += kApos;
	return true;
}

// base + suffix: a vowel meeting a vowel drops the base's (a glottal-stop family writes ' instead; a hiatus
// family keeps both when the pair is allowed).
std::string fuseWith(const FamilyDef& F, std::string base, std::string_view suffix) {
	if (suffix.empty()) return base;
	if (base.empty()) return std::string(suffix);
	const char a = base.back(), b = lowerCh(suffix.front());
	if (isVowelCh(a) && isVowelCh(b)) {
		if (F.apos) return base + "'" + std::string(suffix);
		if (vvOk(F, a, b) && !(base.size() >= 2 && isVowelCh(base[base.size() - 2]))) return base + std::string(suffix);
		base.pop_back();
	}
	return base + std::string(suffix);
}

// Plain fusion (people names, adjectives, plurals): a final vowel gives way to a suffix's first vowel.
std::string fusePlain(std::string base, std::string_view suffix) {
	if (suffix.empty()) return base;
	if (base.empty()) return std::string(suffix);
	if (isVowelCh(base.back()) && isVowelCh(lowerCh(suffix.front()))) base.pop_back();
	return base + std::string(suffix);
}

std::string capWords(std::string_view s) {
	std::string out(s);
	bool start = true;
	for (char& c : out) {
		if (start && c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
		start = (c == ' ' || c == '-');
	}
	return out;
}

int sylCount(const Lang& L, Rng& r) {
	const int i = r.weightedIndex(3, [&L](int k) { return double(L.syl[k]); });
	return i < 0 ? 2 : i + 1;
}

// A fresh specific part of n syllables (its last syllable is the code slot), with `bound` text before it in
// the same word and `suffix` after it. lead: the name's earlier words ("Lusa ", "Tor-").
bool freshSpec(const Lang& L, Rng& r, int cls, int n, const std::string& bound, const std::string& suffix, std::string_view lead, std::string& out) {
	out = bound;
	const int boundSyl = nucleiOf(bound);
	const int total = boundSyl + n + nucleiOf(suffix);
	const int stressAt = stressIndex(L.F(), total);
	freshInto(L, r, out, n - 1, bound.empty(), !bound.empty(), cls, stressAt, boundSyl, false);
	const int follow = suffix.empty() ? 0 : (isVowelCh(lowerCh(suffix[0])) ? 1 : 2);
	const bool slotInitial = n == 1;
	if (!slotInto(L, r, out, lead, slotInitial, slotInitial && !bound.empty(), cls, stressAt == boundSyl + n - 1, follow, suffix)) return false;
	out += suffix;
	return true;
}

// ============================================================ places

std::string pickGeneric(const Lang& L, Rng& r, int cls) {
	const std::string* g = r.pick(L.gens);
	return g ? resolveHarm(*g, cls) : std::string();
}

// One word ending in a generic: a recurring root (when one fits the code) or a fresh specific part.
bool genWord(const Lang& L, Rng& r, std::string_view lead, std::string& out) {
	if (!L.roots.empty() && chancePm(r, L.pRoot)) {
		const std::string& root = *r.pick(L.roots);
		const int cls = L.F().harmony ? std::max(1, classOf(root)) : 0;
		const int start = r.pickIndex(int(L.gens.size()));
		for (size_t k = 0; k < L.gens.size(); k++) {
			const std::string g = resolveHarm(L.gens[(size_t(start) + k) % L.gens.size()], cls);
			const std::string cand = fuseWith(L.F(), root, g);
			if (codeOk(std::string(lead) + cand)) { out = cand; return true; }
		}
	}
	const int cls = startClass(L, r);
	const std::string g = pickGeneric(L, r, cls);
	const int n = std::max(sylCount(L, r), sylsFor(L, cls, listBits(L.gens.size()), 11.0, 1, 3));
	return freshSpec(L, r, cls, n, "", g, lead, out);
}

bool bareWord(const Lang& L, Rng& r, std::string_view lead, std::string& out) {
	const int cls = startClass(L, r);
	const int n = std::max(std::clamp(sylCount(L, r) + 1, 2, 3), sylsFor(L, cls, 0.0, 11.0, 2, 3));
	return freshSpec(L, r, cls, n, "", "", lead, out);
}

bool placeOnce(const Lang& L, Rng& r, std::string& name) {
	int form[6];
	for (int k = 0; k < 6; k++) form[k] = L.form[k];
	if (L.pres.empty()) { form[2] = 0; form[5] = 0; }
	const int f = r.weightedIndex(6, [&form](int k) { return double(form[k]); });
	std::string w;
	switch (f) {
	case 2: {                                                                // "Lusa Amarenne"
		const std::string p = *r.pick(L.pres);
		const std::string lead = p + " ";
		if (!(r.chance(0.6) ? genWord(L, r, lead, w) : bareWord(L, r, lead, w))) return false;
		name = capWords(p + " " + w);
		return true;
	}
	case 3: {                                                                // "Tor-Uzgal"
		std::string first;
		const int cls = startClass(L, r);
		freshInto(L, r, first, r.chance(0.75) ? 1 : 2, true, false, cls, 0, 0, true);
		const std::string lead = first + "-";
		if (!(r.chance(0.6) ? genWord(L, r, lead, w) : bareWord(L, r, lead, w))) return false;
		name = capWords(first + "-" + w);
		return true;
	}
	case 4: {                                                                // "Lelemoa"
		const int cls = startClass(L, r);
		std::string syl;
		if (const Tok* t = pickTok(L.on, r)) syl = t->s;
		syl += pickVowel(L, r, cls, false);
		const std::string base = syl + syl;
		const std::string suffix = r.chance(0.3) ? pickGeneric(L, r, cls) : std::string();
		if (!freshSpec(L, r, cls, suffix.empty() ? r.integer(1, 2) : 1, base, suffix, "", w)) return false;
		name = capWords(w);
		return true;
	}
	case 5: {                                                                // "Kindembe"
		const int cls = startClass(L, r);
		const std::string b = *r.pick(L.pres);
		const std::string suffix = r.chance(0.5) ? pickGeneric(L, r, cls) : std::string();
		if (!freshSpec(L, r, cls, suffix.empty() ? 2 : r.integer(1, 2), b, suffix, "", w)) return false;
		name = capWords(w);
		return true;
	}
	case 1:                                                                  // "Amarenne"
		if (!bareWord(L, r, "", w)) return false;
		name = capWords(w);
		return true;
	default:                                                                 // "Velessa"
		if (!genWord(L, r, "", w)) return false;
		name = capWords(w);
		return true;
	}
}

// Last resort: a plain CV-CV word that satisfies the code (searched in a fixed order from a drawn start).
std::string fallbackCoded(const Lang& L, Rng& r, int hiLen) {
	static const char kC[] = "kmnlrstvd";
	static const char kV[] = "aeiou";
	const int start = r.pickIndex(9 * 5 * 9 * 5 * 9);
	for (int k = 0; k < 9 * 5 * 9 * 5 * 9; k++) {
		int x = (start + k) % (9 * 5 * 9 * 5 * 9);
		std::string w;
		w += char(kC[x % 9] - 'a' + 'A'); x /= 9;
		w += kV[x % 5]; x /= 5;
		w += kC[x % 9]; x /= 9;
		w += kV[x % 5]; x /= 5;
		w += kC[x % 9];
		if (acceptable(w, L, kNamesTune.minLen, hiLen, true)) return w;
		w.pop_back();
		if (acceptable(w, L, kNamesTune.minLen, hiLen, true)) return w;
	}
	return "Kamol";
}

std::string placeIn(const Lang& L, Rng& r) {
	for (int t = 0; t < kNamesTune.tries; t++) {
		std::string name;
		if (placeOnce(L, r, name) && acceptable(name, L, kNamesTune.minLen, kNamesTune.maxLen, true, L.F().maxWord)) return name;
	}
	return fallbackCoded(L, r, kNamesTune.maxLen);
}

// ============================================================ people, peoples, gods

constexpr int kPersonMaxLen = 10;
constexpr int kCultureNameMax = 10;

bool personOnce(const Lang& L, Rng& r, std::string& name) {
	const int cls = L.F().harmony ? (r.chance(0.3) ? 1 : 2) : 0;
	std::string w;
	if (!L.pends.empty() && r.chance(0.65)) {
		const Tok* e = pickTok(L.pends, r);
		const std::string end = e ? resolveHarm(e->s, cls) : std::string();
		const int n = std::min(4, sylsFor(L, cls, h2(L.pends, 0), 12.5, 1, 3) + (r.chance(0.2) ? 1 : 0));
		if (!freshSpec(L, r, cls, n, "", end, "", w)) return false;
	} else {
		const int n = std::min(4, sylsFor(L, cls, 0.0, 12.5, 2, 3) + (r.chance(0.2) ? 1 : 0));
		if (!freshSpec(L, r, cls, n, "", "", "", w)) return false;
	}
	name = capWords(w);
	return true;
}

std::string personIn(const Lang& L, Rng& r) {
	for (int t = 0; t < kNamesTune.tries; t++) {
		std::string name;
		if (personOnce(L, r, name) && acceptable(name, L, kNamesTune.minLen, kPersonMaxLen, true)) return name;
	}
	return fallbackCoded(L, r, kPersonMaxLen);
}

bool cultureOnce(const Lang& L, Rng& r, CultureNames& c) {
	const FamilyDef& F = L.F();
	const int cls = F.harmony ? (r.chance(0.3) ? 1 : 2) : 0;
	std::vector<Tok> ends;
	parseTokens(F.nameEnd, ends, true);
	const Tok* e = pickTok(ends, r);
	const std::string end = e ? resolveHarm(e->s, cls) : std::string();
	const int n = std::min(4, sylsFor(L, cls, h2(ends, 0), 13.0, 1, 4) + (r.chance(0.15) ? 1 : 0));
	std::string stem;
	if (!freshSpec(L, r, cls, n, "", end, "", stem)) return false;
	const std::string name = stem;
	const int nc = F.harmony ? std::max(1, classOf(name)) : 0;
	std::vector<Tok> adjs;
	parseTokens(F.adjEnd, adjs);
	const Tok* a = pickTok(adjs, r);
	const std::string adj = fusePlain(name, a ? resolveHarm(a->s, nc) : std::string("an"));
	const std::string plur = *F.plurEnd ? fusePlain(name, resolveHarm(F.plurEnd, nc)) : name;
	c = CultureNames{ capWords(name), capWords(adj), "the " + capWords(plur) };
	return true;
}

bool cultureOk(const CultureNames& c, const Lang& L) {
	if (!acceptable(c.name, L, kNamesTune.minLen, kCultureNameMax, true)) return false;
	if (!acceptable(c.adj, L, kNamesTune.minLen, kNamesTune.maxLen, false)) return false;
	if (c.plural.size() < 4 || int(c.plural.size()) > kNamesTune.maxLen) return false;
	return acceptable(c.plural.substr(4), L, kNamesTune.minLen, kNamesTune.maxLen, false);
}

CultureNames cultureIn(const Lang& L, Rng& r) {
	for (int t = 0; t < kNamesTune.tries * 2; t++) {
		CultureNames c;
		if (cultureOnce(L, r, c) && cultureOk(c, L)) return c;
	}
	const std::string fb = fallbackCoded(L, r, 7);
	return CultureNames{ fb, fb + "an", "the " + fusePlain(fb, "i") };
}

// The god's word: a divine root and a divine ending of the family (validated), else a fresh word.
std::string makeGod(const Lang& L, Rng& r) {
	const FamilyDef& F = L.F();
	std::vector<std::string> roots, ends;
	parseStrings(F.godRoots, roots);
	parseStrings(F.godEnds, ends);
	for (int t = 0; t < 24 && !roots.empty() && !ends.empty(); t++) {
		const std::string root = *r.pick(roots);
		const int cls = F.harmony ? std::max(1, classOf(root)) : 0;
		const std::string g = fusePlain(root, resolveHarm(*r.pick(ends), cls));
		if (wordOk(g, F, 3, 12)) return g;
	}
	for (int t = 0; t < 24; t++) {
		std::string g;
		freshInto(L, r, g, 2, true, false, startClass(L, r), 0, 0, true);
		if (wordOk(g, F, 3, 12)) return g;
	}
	return "ama";
}

// The god's word when a record has none: the family's first divine root and ending.
std::string makeGodFallback(const Lang& L) {
	const FamilyDef& F = L.F();
	std::vector<std::string> roots, ends;
	parseStrings(F.godRoots, roots);
	parseStrings(F.godEnds, ends);
	if (!roots.empty() && !ends.empty()) {
		const int cls = F.harmony ? std::max(1, classOf(roots[0])) : 0;
		const std::string g = fusePlain(roots[0], resolveHarm(ends[0], cls));
		if (wordOk(g, F, 3, 12)) return g;
	}
	return "ama";
}

// ============================================================ building a language

int jitterP(Rng& r, int v) {
	const int d = v / 6;
	return std::clamp(v + r.integer(-d, d), 0, 1000);
}

// Keeps most of an inventory, jitters its weights; at least min(3, size) sounds survive.
void thinTokens(Rng& r, std::vector<Tok>& v, int dropPm) {
	const size_t goal = std::min<size_t>(3, v.size());
	std::vector<Tok> kept;
	for (size_t i = 0; i < v.size(); i++) {
		const size_t left = v.size() - i;
		const size_t need = goal > kept.size() ? goal - kept.size() : 0;
		if (left > need && v.size() > 3 && chancePm(r, dropPm)) continue;
		Tok k = v[i];
		k.w = std::max(1, v[i].w + r.integer(-1, 1));
		kept.push_back(k);
	}
	v = kept;
}

std::vector<std::string> takeSome(Rng& r, const char* pool, int lo, int hi) {
	std::vector<std::string> all;
	parseStrings(pool, all);
	r.shuffle(all);
	const int k = std::min(int(all.size()), r.integer(lo, hi));
	all.resize(size_t(std::max(0, k)));
	return all;
}

std::vector<std::string> makeRoots(const Lang& L, Rng& r, int count, const std::vector<std::string>& have) {
	std::vector<std::string> out;
	for (int guard = 0; guard < count * 24 && int(out.size()) < count; guard++) {
		std::string root;
		const int cls = startClass(L, r);
		freshInto(L, r, root, r.chance(0.35) ? 1 : 2, true, false, cls, 0, 0, false);
		if (root.find('\'') != std::string::npos && root.back() == '\'') continue;
		if (!wordOk(root, L.F(), 2, 7)) continue;
		if (std::find(out.begin(), out.end(), root) != out.end()) continue;
		if (std::find(have.begin(), have.end(), root) != have.end()) continue;
		out.push_back(root);
	}
	return out;
}

Lang rootLang(Rng& r, int fam, bool cheap) {
	Lang L;
	L.fam = std::clamp(fam, 0, kFamilies - 1);
	const FamilyDef& F = L.F();
	parseTokens(F.on, L.on);
	parseTokens(F.om, L.om);
	parseTokens(F.vo, L.vo);
	parseTokens(F.fr, L.fr);
	parseTokens(F.bk, L.bk);
	parseTokens(F.lo, L.lo);
	parseTokens(F.cm, L.cm);
	parseTokens(F.cf, L.cf);
	thinTokens(r, L.on, 120);
	thinTokens(r, L.om, 120);
	thinTokens(r, L.vo, 60);
	thinTokens(r, L.fr, 0);
	thinTokens(r, L.bk, 0);
	thinTokens(r, L.lo, 150);
	thinTokens(r, L.cm, 120);
	thinTokens(r, L.cf, 120);
	L.pe0 = jitterP(r, F.pe0);
	L.pe1 = jitterP(r, F.pe1);
	L.pcm = jitterP(r, F.pcm);
	L.pcf = jitterP(r, F.pcf);
	L.pl = jitterP(r, F.pl);
	L.pg = jitterP(r, F.pg);
	L.gem = F.gem;
	const int syl[3] = { F.syl1, F.syl2, F.syl3 };
	for (int k = 0; k < 3; k++) L.syl[k] = syl[k] > 0 ? std::max(1, syl[k] + r.integer(-1, 1)) : 0;
	const int form[6] = { F.fGen, F.fBare, F.fTwo, F.fHyph, F.fRedup, F.fPre };
	for (int k = 0; k < 6; k++) L.form[k] = form[k] > 0 ? std::max(1, jitterP(r, form[k] * 10) / 10) : 0;
	L.pRoot = jitterP(r, F.pRoot);
	L.gens = takeSome(r, F.gens, 15, 18);
	L.pres = takeSome(r, F.pres, 3, 5);
	parseTokens(F.pends, L.pends);
	thinTokens(r, L.pends, 150);
	L.titles = takeSome(r, F.titles, 2, 2);
	L.ships = takeSome(r, F.ships, 3, 3);
	L.god = makeGod(L, r);
	if (!cheap) L.roots = makeRoots(L, r, 22, {});
	finalize(L, true);
	return L;
}

// ---- sound laws

bool digraphGuard(std::string_view w, size_t i, std::string_view from) {
	if (from.size() != 1) return false;
	const char c = from[0];
	const char next = i + 1 < w.size() ? w[i + 1] : 0;
	const char prev = i > 0 ? w[i - 1] : 0;
	if (next == 'h' && std::strchr("kgzstpc", c)) return true;       // kh gh zh sh th ph ch
	if (c == 'h' && prev && std::strchr("kgzstpc", prev)) return true;
	if (c == 'n' && next && std::strchr("dgjzy", next)) return true;   // nd ng nj nz ny
	if (c == 'm' && next == 'b') return true;                         // mb
	if ((c == 'k' || c == 'g') && next == 'w') return true;           // kw gw
	if ((c == 'g' || c == 'd' || c == 'j' || c == 'z' || c == 'y') && prev == 'n') return true;
	if (c == 'b' && prev == 'm') return true;
	return false;
}

std::string lawStr(std::string_view w, const LawRule& L) {
	const std::string_view from(L.from);
	std::string out;
	size_t i = 0;
	while (i < w.size()) {
		bool hit = w.compare(i, from.size(), from) == 0 && !digraphGuard(w, i, from);
		if (hit) {
			const size_t end = i + from.size();
			switch (L.where) {
			case 'I': hit = i == 0 || isSep(w[i - 1]); break;
			case 'F': hit = end == w.size() || isSep(w[end]); break;
			case 'V': hit = i > 0 && isVowelCh(w[i - 1]) && end < w.size() && isVowelCh(w[end]); break;
			default: break;
			}
		}
		if (hit) {
			out += L.to;
			i += from.size();
		} else {
			out += w[i++];
		}
	}
	return out;
}

// pos: 'I' word-initial onsets, 'M' inner onsets, 'N' nuclei, 'C' inner codas, 'E' final codas.
void lawTokens(std::vector<Tok>& v, char pos, const LawRule& L) {
	const std::string_view from(L.from);
	for (Tok& t : v) {
		const std::string_view s(t.s);
		if (L.where == 'A') {
			t.s = lawStr(s, L);
		} else if (L.where == 'I' && pos == 'I') {
			if (s.substr(0, from.size()) == from && !digraphGuard(s, 0, from)) t.s = std::string(L.to) + std::string(s.substr(from.size()));
		} else if (L.where == 'V' && pos == 'M') {
			if (s.substr(0, from.size()) == from && !digraphGuard(s, 0, from)) t.s = std::string(L.to) + std::string(s.substr(from.size()));
		} else if (L.where == 'F' && pos == 'E' && !isVowelCh(from[0])) {
			if (s.size() >= from.size() && s.substr(s.size() - from.size()) == from && !digraphGuard(s, s.size() - from.size(), from))
				t.s = std::string(s.substr(0, s.size() - from.size())) + L.to;
		}
	}
	std::vector<Tok> kept;
	for (Tok& t : v) if (!t.s.empty() && pronounceable(t.s)) kept.push_back(std::move(t));
	v = std::move(kept);
	mergeTokens(v);
}

// A shifted word when it is still sound, else the old one.
std::string shiftWord(std::string_view w, int id, const FamilyDef& F, size_t lo, size_t hi) {
	std::string t(w);
	for (int k = 0; k < kLawRules; k++) {
		if (kLaws[k].id == id) t = lawStr(t, kLaws[k]);
	}
	return wordOk(t, F, lo, hi) ? t : std::string(w);
}

void applyLaw(Lang& D, int id) {
	const FamilyDef& F = D.F();
	for (int k = 0; k < kLawRules; k++) {
		if (kLaws[k].id != id) continue;
		const LawRule& L = kLaws[k];
		auto safe = [&](std::vector<Tok>& v, char pos) {
			const std::vector<Tok> before = v;
			lawTokens(v, pos, L);
			if (v.empty()) v = before;
		};
		safe(D.on, 'I');
		safe(D.om, 'M');
		safe(D.vo, 'N');
		safe(D.fr, 'N');
		safe(D.bk, 'N');
		safe(D.lo, 'N');
		safe(D.cm, 'C');
		safe(D.cf, 'E');
	}
	for (std::string& s : D.roots) s = shiftWord(s, id, F, 2, 8);
	for (std::string& s : D.gens) s = shiftWord(s, id, F, 2, 7);
	for (std::string& s : D.pres) s = shiftWord(s, id, F, 1, 7);
	for (std::string& s : D.titles) s = shiftWord(s, id, F, 3, 10);
	for (std::string& s : D.ships) s = shiftWord(s, id, F, 3, 9);
	for (Tok& t : D.pends) t.s = shiftWord(t.s, id, F, 1, 5);
	D.god = shiftWord(D.god, id, F, 3, size_t(kNamesTune.maxLen));
	if (id == 13) D.gem.erase(std::remove(D.gem.begin(), D.gem.end(), 's'), D.gem.end());
	if (id == 28) D.gem.erase(std::remove_if(D.gem.begin(), D.gem.end(), [](char c) { return c == 'k' || c == 't' || c == 'p'; }), D.gem.end());
	D.laws.push_back(id);
}

// A daughter: the mother's language after one or two of the family's sound laws, part of her roots and
// place endings renewed from the family's stock, and now and then words borrowed from a sister family.
Lang daughterOf(const Lang& parent, Rng& r) {
	Lang D = parent;
	const FamilyDef& F = D.F();
	D.gen = parent.gen + 1;
	// sound laws: those of the family not yet taken and that change something here, in a drawn order
	std::vector<int> pool;
	parseIntList(F.laws, pool);
	std::vector<int> cands;
	const std::string before = writeLang(D);
	for (int id : pool) {
		if (std::find(D.laws.begin(), D.laws.end(), id) != D.laws.end()) continue;
		Lang probe = D;
		applyLaw(probe, id);
		probe.laws = D.laws;
		if (writeLang(probe) != before) cands.push_back(id);
	}
	r.shuffle(cands);
	const int room = kMaxLaws - int(D.laws.size());
	const int want = std::min({ int(cands.size()), room, std::clamp(r.integer(1, 2), 1, std::max(1, kNamesTune.newShifts)) });
	for (int i = 0; i < want; i++) applyLaw(D, cands[size_t(i)]);
	// weights drift
	for (std::vector<Tok>* v : { &D.on, &D.om, &D.cm, &D.cf }) {
		for (Tok& t : *v) if (r.chance(0.3)) t.w = std::clamp(t.w + r.integer(-1, 1), 1, 99);
	}
	// place endings: about a third renewed from the family's stock (through the language's own laws)
	std::vector<std::string> stock;
	parseStrings(F.gens, stock);
	r.shuffle(stock);
	size_t si = 0;
	for (std::string& g : D.gens) {
		if (!r.chance(0.33)) continue;
		while (si < stock.size()) {
			std::string cand = stock[si++];
			for (int id : D.laws) cand = shiftWord(cand, id, F, 2, 7);
			if (std::find(D.gens.begin(), D.gens.end(), cand) == D.gens.end()) { g = cand; break; }
		}
	}
	// contact: a sister family lends three place endings
	if (D.contact < 0 && r.chance(0.3)) {
		D.contact = F.sister;
		std::vector<std::string> lent;
		parseStrings(kFam[F.sister].gens, lent);
		r.shuffle(lent);
		int done = 0;
		for (const std::string& g : lent) {
			if (done >= 3 || D.gens.empty()) break;
			if (!wordOk(g, F, 2, 7)) continue;
			if (std::find(D.gens.begin(), D.gens.end(), g) != D.gens.end()) continue;
			D.gens[size_t(r.pickIndex(int(D.gens.size())))] = resolveHarm(g, 2);
			done++;
		}
	}
	// roots: about 60 % kept, the rest coined anew
	std::vector<std::string> kept;
	for (const std::string& s : D.roots) if (r.chance(0.6)) kept.push_back(s);
	const std::vector<std::string> fresh = makeRoots(D, r, std::max(0, 22 - int(kept.size())), kept);
	kept.insert(kept.end(), fresh.begin(), fresh.end());
	const size_t cap = size_t(std::min(32, kNamesTune.maxRoots));
	if (kept.size() > cap) kept.resize(cap);
	D.roots = kept;
	// sometimes a new prefix word or title
	if (!D.pres.empty() && r.chance(0.3)) {
		std::vector<std::string> ps;
		parseStrings(F.pres, ps);
		std::string p = *r.pick(ps);
		for (int id : D.laws) p = shiftWord(p, id, F, 1, 7);
		D.pres[size_t(r.pickIndex(int(D.pres.size())))] = p;
	}
	finalize(D, true);
	return D;
}

// The variety of a language: the share of distinct names among 300 places drawn by a probe rng seeded
// from the language's own text (so the answer depends on the language alone, never on the caller's rng).
double varietyOf(const Lang& L) {
	Rng probe(hashStr(writeLang(L)));
	const int n = 150;
	std::set<std::string> ps, qs, cs;
	for (int k = 0; k < n; k++) ps.insert(placeIn(L, probe));
	for (int k = 0; k < n; k++) qs.insert(personIn(L, probe));
	for (int k = 0; k < n; k++) cs.insert(cultureIn(L, probe).name);
	return double(std::min({ ps.size(), qs.size(), cs.size() })) / double(n);
}

// ---- reading any text as a language

// dm2 records as they are; old dm1 records and anything else become a language derived from a hash of the
// text (no roots, so this stays cheap), and a dm1 record keeps its god's word when that word is sound.
Lang parseAny(std::string_view text) {
	Lang L;
	if (parseDm2(text, L)) return L;
	Rng lr(hashStr(text));
	L = rootLang(lr, lr.pickIndex(kFamilies), true);
	if (text.size() >= 3 && text.substr(0, 3) == "dm1") {
		const size_t at = text.find(";gd=");
		if (at != std::string_view::npos) {
			const size_t end = text.find(';', at + 4);
			const std::string g = lettersLower(text.substr(at + 4, end == std::string_view::npos ? std::string_view::npos : end - at - 4));
			bool lower = true;
			for (char c : text.substr(at + 4, end == std::string_view::npos ? std::string_view::npos : end - at - 4)) lower = lower && c >= 'a' && c <= 'z';
			if (lower && g.size() >= 3 && g.size() <= size_t(kNamesTune.maxLen) && pronounceable(g) && !blocked(g)) L.god = g;
		}
	}
	return L;
}

bool isParent(std::string_view text) {
	return (text.size() >= 4 && text.substr(0, 4) == "dm2;") || (text.size() >= 3 && text.substr(0, 3) == "dm1");
}

// ============================================================ planets (language-agnostic)

const char* const kPlanetEnds[] = { "ia", "on", "ar", "is", "ara", "ine", "eth", "or", "el", "os" };
const char* const kEvoA[] = { "Ember", "Gloam", "Hollow", "Ash", "Mere", "Sable", "Pale", "Dawn", "Thorn", "Star", "Deep", "Mist", "Salt", "Iron", "Glass", "Veil", "Amber", "Cinder", "Frost", "Silver", "Umber", "Lark", "Wren", "Halcyon", "Vesper", "Ivory" };
const char* const kEvoB[] = { "reach", "hold", "watch", "fall", "haven", "drift", "march", "shore", "vale", "spire", "wake", "glen", "cairn", "sound", "keep", "well", "gate", "fen", "crest", "mere" };
const char* const kPlanetOn[] = { "l", "m", "n", "r", "s", "v", "th", "d", "k", "t", "f", "h", "c", "sel", "ael" };
const char* const kPlanetV[] = { "a", "e", "i", "o", "u", "ae", "ia", "ei" };
const char* const kPlanetCoda[] = { "", "", "", "n", "l", "r", "s", "th" };

bool isRealPlanetName(const std::string& s) {
	static const char* const kReal[] = { "earth", "mars", "venus", "terra", "gaia", "gaea", "tellus", "pluto", "saturn", "neptune", "uranus",
		"mercury", "jupiter", "luna", "titan", "eris", "ceres", "europa", "vulcan", "nibiru", "arrakis", "tatooine", "endor" };
	const std::string low = lettersLower(s);
	for (const char* x : kReal) {
		if (low == x) return true;
	}
	return false;
}

std::string planetOnce(Rng& r) {
	if (r.chance(0.5)) {
		std::string s;
		const int n = r.integer(2, 3);
		for (int i = 0; i < n; i++) {
			s += kPlanetOn[r.pickIndex(int(sizeof(kPlanetOn) / sizeof(kPlanetOn[0])))];
			s += kPlanetV[r.pickIndex(int(sizeof(kPlanetV) / sizeof(kPlanetV[0])))];
			if (i + 1 < n && r.chance(0.3)) s += kPlanetCoda[r.pickIndex(int(sizeof(kPlanetCoda) / sizeof(kPlanetCoda[0])))];
		}
		if (r.chance(0.35)) s = fusePlain(s, kPlanetEnds[r.pickIndex(int(sizeof(kPlanetEnds) / sizeof(kPlanetEnds[0])))]);
		return capWords(s);
	}
	const std::string a = kEvoA[r.pickIndex(int(sizeof(kEvoA) / sizeof(kEvoA[0])))];
	const std::string b = kEvoB[r.pickIndex(int(sizeof(kEvoB) / sizeof(kEvoB[0])))];
	if (r.chance(0.56)) return a + b;
	return a + " " + capWords(b);
}

} // namespace

// ============================================================ the public API

std::string cap(const std::string& s) {
	std::string r = s;
	if (!r.empty() && r[0] >= 'a' && r[0] <= 'z') r[0] = char(r[0] - 'a' + 'A');
	return r;
}

std::string language(Rng& rng, const std::string& parent) {
	const bool haveParent = isParent(parent);
	Lang old;
	if (haveParent) old = parseAny(parent);
	// A language whose names repeat too often is drawn again, a few rounds at most; the most varied draw is
	// kept. The rng decides the draws; varietyOf depends on the language alone.
	Lang best;
	double bestShare = -1.0;
	const int fam = haveParent ? old.fam : rng.pickIndex(kFamilies);   // redraws stay in the family
	for (int round = 0; round < std::max(1, kNamesTune.rounds); round++) {
		Lang cand = haveParent ? daughterOf(old, rng) : rootLang(rng, fam, false);
		const double share = varietyOf(cand);
		if (share > bestShare) { bestShare = share; best = std::move(cand); }
		if (share >= 0.93) break;
	}
	return writeLang(best);
}

std::string word(const std::string& lang, Rng& rng, int minSyl, int maxSyl) {
	const Lang L = parseAny(lang);
	const int lo = std::clamp(std::min(minSyl, maxSyl), 1, 5);
	const int hi = std::clamp(std::max(minSyl, maxSyl), lo, 5);
	for (int t = 0; t < kNamesTune.tries * 2; t++) {
		std::string w;
		if (!L.roots.empty() && rng.chance(0.3)) {
			const std::string& root = *rng.pick(L.roots);
			const int n = nucleiOf(root);
			if (n >= lo && n <= hi) w = root;
		}
		if (w.empty()) freshInto(L, rng, w, rng.integer(lo, hi), true, false, startClass(L, rng), 0, 0, true);
		w = lettersLower(w);
		const int n = int(w.size());
		if (n >= 3 && n <= kNamesTune.maxLen && pronounceable(w) && !blocked(w) && !forbiddenIn(w, L.F())) return w;
	}
	return lettersLower(fallbackCoded(L, rng, kNamesTune.maxLen));
}

std::string place(const std::string& lang, Rng& rng) {
	return placeIn(parseAny(lang), rng);
}

std::string person(const std::string& lang, Rng& rng) {
	return personIn(parseAny(lang), rng);
}

CultureNames culture(const std::string& lang, Rng& rng) {
	return cultureIn(parseAny(lang), rng);
}

std::string god(const std::string& lang, Rng& rng) {
	static_cast<void>(rng);   // the god's word is fixed in the language, so related cultures share it
	const Lang L = parseAny(lang);
	if (!wordOk(L.god, L.F(), 3, size_t(kNamesTune.maxLen))) return cap(makeGodFallback(L));
	return cap(L.god);
}

std::string planet(Rng& rng) {
	for (int t = 0; t < 40; t++) {
		const std::string name = planetOnce(rng);
		const int n = int(name.size());
		if (n >= kNamesTune.minLen && n <= kNamesTune.maxLen && pronounceable(name) && !blocked(name) && !isRealPlanetName(name)) return name;
	}
	return "Caelith";
}

std::string ruler(const std::string& lang, Rng& rng) {
	const Lang L = parseAny(lang);
	for (int t = 0; t < kNamesTune.tries; t++) {
		const std::string title = L.titles.empty() ? std::string() : capWords(*rng.pick(L.titles));
		const std::string who = personIn(L, rng);
		const std::string s = title.empty() ? who : title + " " + who;
		if (s.size() <= 24 && pronounceable(s) && !blocked(s)) return s;
	}
	return personIn(L, rng);
}

std::string epithet(const std::string& lang, Rng& rng) {
	const Lang L = parseAny(lang);
	std::vector<std::string> own, common;
	eachPiece(L.F().epithets, ';', [&own](std::string_view e) { if (!e.empty()) own.emplace_back(e); });
	if (L.contact >= 0 && rng.chance(0.15)) {
		eachPiece(kFam[L.contact].epithets, ';', [&own](std::string_view e) { if (!e.empty()) own.emplace_back(e); });
	}
	eachPiece(kCommonEpithets, ';', [&common](std::string_view e) { if (!e.empty()) common.emplace_back(e); });
	const std::vector<std::string>& src = (rng.chance(0.7) && !own.empty()) ? own : common;
	return *rng.pick(src);
}

std::string ship(const std::string& lang, Rng& rng) {
	const Lang L = parseAny(lang);
	for (int t = 0; t < kNamesTune.tries; t++) {
		std::string w;
		if (!L.ships.empty() && rng.chance(0.7)) {
			const std::string first = *rng.pick(L.ships);
			const std::string lead = first + " ";
			if (!(rng.chance(0.5) ? genWord(L, rng, lead, w) : bareWord(L, rng, lead, w))) continue;
			const std::string s = capWords(first + " " + w);
			if (s.size() <= 24 && pronounceable(s) && !blocked(s)) return s;
		} else {
			const std::string p = placeIn(L, rng);
			if (p.find(' ') == std::string::npos) return p;
		}
	}
	return placeIn(L, rng);
}

} // namespace dm::names
