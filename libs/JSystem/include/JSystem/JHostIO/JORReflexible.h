#ifndef JORREFLEXIBLE_H
#define JORREFLEXIBLE_H

#include <types.h>
#include "JSystem/JHostIO/JORServer.h"

struct JOREvent;
struct JORPropertyEvent;
struct JORGenEvent;
struct JORNodeEvent;

class JORMContext;
class JORServer;

class JOREventListener {
public:
#if DEBUG
    JOREventListener() {}
#if TARGET_PC
    virtual void listenPropertyEvent(const JORPropertyEvent*) {}
#else
    virtual void listenPropertyEvent(const JORPropertyEvent*) = 0;
#endif
#endif
};

class JORReflexible : public JOREventListener {
public:
    static JORServer* getJORServer() { return JORServer::getInstance(); }

#if DEBUG
    JORReflexible() {}

    virtual void listenPropertyEvent(const JORPropertyEvent*);
    virtual void listen(u32, const JOREvent*);
    virtual void genObjectInfo(const JORGenEvent*);
#if TARGET_PC
    virtual void genMessage(JORMContext*) {}
#else
    virtual void genMessage(JORMContext*) = 0;
#endif
    virtual void listenNodeEvent(const JORNodeEvent*);
#endif
};

#endif /* JORREFLEXIBLE_H */
