#include "redbound/Trigger.h"
#include "redbound/World.h"
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace redbound;

static int g_failed = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

struct MockWorld;
static Trigger* g_selfTrigger = 0;
static Trigger* g_otherTrigger = 0;

struct MockWorld : World {
	std::map<std::string, float> vars, persistent;
	std::vector<std::string> log;
	std::set<std::string> keys;
	float getVariable(const std::string& n) const override { std::map<std::string, float>::const_iterator i = vars.find(n); return i == vars.end() ? 0.f : i->second; }
	void setVariable(const std::string& n, float v) override { vars[n] = v; }
	void setPersistentVariable(const std::string& n, float v) override { persistent[n] = v; }
	void setTriggerEnabled(const std::string& t, bool e) override {
		log.push_back((e ? "enable " : "disable ") + t);
		if (g_selfTrigger && g_selfTrigger->name() == t) g_selfTrigger->finishThenSet(e ? Trigger::ENABLED : Trigger::DISABLED);
		if (g_otherTrigger && g_otherTrigger->name() == t) g_otherTrigger->setState(e ? Trigger::ENABLED : Trigger::DISABLED);
	}
	void activateTrigger(const std::string& t) override { log.push_back("activate " + t); }
	void link(const std::string& p, const std::vector<std::string>& n) override { std::string s = "link " + p; for (auto& x : n) s += " " + x; log.push_back(s); }
	void unlink(const std::vector<std::string>& n) override { std::string s = "unlink"; for (auto& x : n) s += " " + x; log.push_back(s); }
	bool isKeyDown(const std::string& k) const override { return keys.count(k) != 0; }
	std::map<int, int> anims;
	int playAnimation(const std::string& node, const std::string&, float, float, bool) override {
		const int id = (int)anims.size(); anims[id] = 3; log.push_back("anim " + node); return id;
	}
	bool animationDone(int id) const override { std::map<int, int>::const_iterator i = anims.find(id); return i == anims.end() || i->second <= 0; }
	void tick() { for (auto& a : anims) if (a.second > 0) --a.second; }
	float fade = 0.f;
	int startTransform(TransformKind k, const std::string& o, const std::string&, float, float, float, float) override {
		log.push_back(std::string(k == UV ? "uv " : k == ROTATE ? "rot " : "move ") + o); return 100;
	}
	bool transformDone(int) const override { return true; }
	void setFade(float a, int) override { fade = a; }
	bool genericAction(const std::string& name, const std::vector<std::string>& args) override {
		std::string s = "generic " + name; for (auto& x : args) s += " " + x; log.push_back(s); return true;
	}
};

static void run(Trigger& t, MockWorld& w, int frames, float dt = 1.f / 60.f) { for (int i = 0; i < frames; ++i) t.update(w, dt); }

static void testConditionalSkip() {
	printf("condicion falsa salta al destino\n");
	const char* p = "state=2\nexecutetype=0\na01=equal,mode,1,4\na02=link,scene,N_a\na03=unlink,N_b\na04=nop\n";
	{
		MockWorld w; Trigger t("T_test"); t.parse(p); run(t, w, 5);
		CHECK(w.log.empty());
		CHECK(t.state() == Trigger::DISABLED);
	}
	{
		MockWorld w; w.vars["mode"] = 1; Trigger t("T_test"); t.parse(p); run(t, w, 5);
		CHECK(w.log.size() == 2);
		CHECK(w.log.size() > 0 && w.log[0] == "link scene N_a");
		CHECK(w.log.size() > 1 && w.log[1] == "unlink N_b");
	}
}

static void testDelay() {
	printf("delay waits for the given time\n");
	MockWorld w; Trigger t("T_delay");
	t.parse("state=2\na01=delay,0.5\na02=activatetrigger,T_next\n");
	run(t, w, 20);
	CHECK(w.log.empty());
	run(t, w, 15);
	CHECK(w.log.size() == 1 && w.log[0] == "activate T_next");
}

static void testRandomDelay() {
	printf("random stores a value that delay waits for (eye blinking)\n");
	MockWorld w; Trigger t("T_blink");
	t.parse("state=2\na01=random,blinkDelay,0.5,0.5\na02=delay,blinkDelay\na03=activatetrigger,T_next\n");
	run(t, w, 20);
	CHECK(w.getVariable("blinkDelay") > 0.49f && w.getVariable("blinkDelay") < 0.51f);
	CHECK(w.log.empty());
	run(t, w, 15);
	CHECK(w.log.size() == 1 && w.log[0] == "activate T_next");
}

static void testLoop() {
	printf("loop with counter and exit by condition\n");
	MockWorld w; Trigger t("T_loop");
	t.parse("state=2\nexecutetype=0\na01=add,n,1\na02=less,n,3,5\na03=nop\na04=jumptoaction,1\na05=set,done,1\n");
	run(t, w, 30);
	CHECK(w.vars["n"] == 3.f);
	CHECK(w.vars["done"] == 1.f);
	CHECK(t.state() == Trigger::DISABLED);
}

static void testWaitWithoutTarget() {
	printf("condition without target waits until true\n");
	MockWorld w; Trigger t("T_wait");
	t.parse("state=2\na01=equal,ready,1\na02=link,scene,N_x\n");
	run(t, w, 10);
	CHECK(w.log.empty());
	w.vars["ready"] = 1;
	run(t, w, 2);
	CHECK(w.log.size() == 1);
}

static void testExitListAndRepeat() {
	printf("exit list and executetype\n");
	MockWorld w; Trigger t("T_rep");
	t.parse("a01=set,x,1\ne01=enabletrigger,T_other\n");
	CHECK(t.state() == Trigger::ENABLED);
	run(t, w, 3);
	CHECK(w.log.empty());
	t.activate();
	run(t, w, 3);
	CHECK(w.vars["x"] == 1.f);
	CHECK(w.log.size() == 1 && w.log[0] == "enable T_other");
	CHECK(t.state() == Trigger::ENABLED);
}

static void testUnknownAndGeneric() {
	printf("acciones desconocidas y genericas\n");
	MockWorld w; Trigger t("T_misc");
	std::vector<std::string> warn = t.parse("state=2\na01=noexiste,1\na02=playsound,logs_lo.wav\n");
	CHECK(warn.size() == 1);
	run(t, w, 2);
	CHECK(w.log.size() == 1 && w.log[0] == "generic playsound logs_lo.wav");
}

static void testAnimate() {
	printf("animate without wait continues; with 1 waits until done\n");
	MockWorld w; Trigger t("T_anim");
	t.parse("state=2\na01=animate,A\na02=animate,B,1\na03=set,after,1\n");
	t.update(w, 0.016f);
	CHECK(w.log.size() == 2);
	CHECK(w.vars["after"] == 0.f);
	for (int i = 0; i < 5; ++i) { w.tick(); t.update(w, 0.016f); }
	CHECK(w.vars["after"] == 1.f);
}

static void testFade() {
	printf("fade waits and moves the value from 1 to 0 (fade in) or 0 to 1 (fade out)\n");
	MockWorld w; Trigger t("T_fade");
	t.parse("state=2\na01=fade,1,0,0\na02=set,mid,1\na03=fade,1,1,0\na04=set,end,1\n");
	run(t, w, 30);
	CHECK(w.fade > 0.4f && w.fade < 0.6f);
	CHECK(w.vars["mid"] == 0.f);
	run(t, w, 35);
	CHECK(w.vars["mid"] == 1.f);
	run(t, w, 65);
	CHECK(w.vars["end"] == 1.f);
	CHECK(w.fade < 0.01f);
}

static void testDisableSelf() {
	printf("a trigger that disables itself finishes its list and is not re-enabled\n");
	MockWorld w; Trigger t("T_once");
	g_selfTrigger = &t;
	t.parse("a01=set,n,1\na02=disabletrigger,T_once\na03=set,after,1\n");
	t.activate();
	run(t, w, 3);
	CHECK(w.vars["n"] == 1.f);
	CHECK(w.vars["after"] == 1.f);
	CHECK(t.state() == Trigger::DISABLED);
	t.activate();
	w.vars["n"] = 0;
	run(t, w, 3);
	CHECK(w.vars["n"] == 0.f);
	g_selfTrigger = 0;
}

static void testDisableOther() {
	printf("disabling another looping trigger stops it immediately (T_setstartpos)\n");
	MockWorld w;
	Trigger loop("T_setstartpos"), boss("T_restartLevel");
	g_otherTrigger = &loop;
	loop.parse("a01=add,held,1\na02=jumptoaction,1\n");
	boss.parse("a01=delay,0.5\na02=disabletrigger,T_setstartpos\na03=set,go,1\n");
	loop.activate();
	boss.activate();
	for (int i = 0; i < 120; ++i) { loop.update(w, 1.f / 60); boss.update(w, 1.f / 60); }
	const float held = w.vars["held"];
	CHECK(loop.state() == Trigger::DISABLED);
	CHECK(w.vars["go"] == 1.f);
	for (int i = 0; i < 60; ++i) loop.update(w, 1.f / 60);
	CHECK(w.vars["held"] == held);
	g_otherTrigger = 0;
}

static void testTimer() {
	printf("timer counts the variable down and continues at zero (level 2 T_timer)\n");
	MockWorld w; Trigger t("T_timer");
	w.vars["timer01"] = 1.f;
	t.parse("a01=timer,timer01\na02=set,done,1\n");
	t.activate();
	run(t, w, 30);
	CHECK(w.vars["done"] == 0.f && w.vars["timer01"] > 0.4f && w.vars["timer01"] < 0.6f);
	w.vars["timer01"] += 9.f;
	run(t, w, 60);
	CHECK(w.vars["done"] == 0.f);
	run(t, w, 600);
	CHECK(w.vars["done"] == 1.f && w.vars["timer01"] == 0.f);
}

static void testOriginalTable() {
	printf("table of the 88 original actions\n");
	CHECK(ActionRegistry::originalNames().size() == 88);
	for (const std::string& n : ActionRegistry::originalNames()) CHECK(ActionRegistry::instance().has(n));
}

int main() {
	testConditionalSkip();
	testDelay();
	testRandomDelay();
	testLoop();
	testWaitWithoutTarget();
	testExitListAndRepeat();
	testUnknownAndGeneric();
	testAnimate();
	testFade();
	testDisableSelf();
	testTimer();
	testDisableOther();
	testOriginalTable();
	printf("%d checks, %d failures\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
