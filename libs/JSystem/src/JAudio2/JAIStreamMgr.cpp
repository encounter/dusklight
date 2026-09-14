#if TARGET_PC
#include "JSystem/JAudio2/JASPCMStream.h"
#endif
#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/JAudio2/JAIStreamMgr.h"
#include "JSystem/JAudio2/JAISoundHandles.h"
#include "JSystem/JAudio2/JAIStreamDataMgr.h"
#include "JSystem/JAudio2/JAISoundInfo.h"
#if TARGET_PC
#include "dusk/mods/svc/audio_res/bst.hpp"
#endif

JAIStreamMgr::JAIStreamMgr(bool setInstance) : JASGlobalInstance<JAIStreamMgr>(setInstance) {
    streamDataMgr_ = NULL;
    mStreamAramMgr = NULL;
    field_0x6c = NULL;
    mAudience = NULL;
    mParams.init();
    mActivity.init();
}

bool JAIStreamMgr::startSound(JAISoundID id, JAISoundHandle* handle, const JGeometry::TVec3<f32>* posPtr IF_DUSK_ARG(std::shared_ptr<dusk::mods::svc::audio_res::bst::StreamReplacementSlot> replacement)) {
#if TARGET_PC
    if (replacement && replacement->pcmStream) {
        return startPcmSound(id, handle, posPtr, std::move(replacement));
    }
#endif
    JUT_ASSERT(37, streamDataMgr_);
    if (handle != NULL && *handle) {
        (*handle)->stop();
    }

    s32 streamFileEntry = streamDataMgr_->getStreamFileEntry(id IF_DUSK_ARG(replacement.get()));
    if (streamFileEntry < 0) {
        JUT_WARN(46, "Cannot find the stream file entry for ID:%08x\n", id.id_.composite_)
        return false;
    } 

    JAIStream* stream = newStream_();
    JAISoundInfo* soundInfo = JASGlobalInstance<JAISoundInfo>::getInstance();

    int category = -1;
    if (soundInfo != NULL) {
        category = soundInfo->getCategory(id);
    }

    if (stream == NULL) {
        return false;
    }

    stream->JAIStreamMgr_startID_(id, streamFileEntry, posPtr, mAudience, category IF_DUSK_ARG(replacement));
    if (soundInfo != NULL) {
        soundInfo->getStreamInfo(id, stream IF_DUSK_ARG(replacement.get()));
    }

    if (handle != NULL) {
        stream->attachHandle(handle);
    }

    return false;
}

#if TARGET_PC
bool JAIStreamMgr::startPcmSound(JAISoundID id, JAISoundHandle* handle,
    const JGeometry::TVec3<f32>* posPtr,
    std::shared_ptr<dusk::mods::svc::audio_res::bst::StreamReplacementSlot> replacement) {
    auto* stream = JKR_NEW JAIStream{this, field_0x6c};
    if (!stream) {
        return false;
    }
    auto* info = JASGlobalInstance<JAISoundInfo>::getInstance();
    stream->JAIStreamMgr_startID_(
        id, -1, posPtr, mAudience, info ? info->getCategory(id) : -1, replacement);
    stream->pcmStream_ = replacement->pcmStream;
    if (info) {
        info->getStreamInfo(id, stream, replacement.get());
    }
    stream->getAuxiliary().moveVolume(replacement->initialVolume, 0);
    stream->getAuxiliary().movePitch(replacement->initialPitch, 0);
    if (stream->pcmStream_->attach() != JASPCMStream::Error::NONE) {
        stream->die_JAIStream_();
        JKR_DELETE(stream);
        return false;
    }
    mStreamList.append(stream);
    if (handle) {
        stream->attachHandle(handle);
    }
    return true;
}
#endif

void JAIStreamMgr::freeDeadStream_() {
    JSULink<JAIStream>* i = mStreamList.getFirst();
    while (i != NULL) {
        JAIStream* stream = i->getObject();
        JSULink<JAIStream>* next = i->getNext();
        if (stream->status_.isDead()) {
            mStreamList.remove(i);
            void* aramAddr = stream->JAIStreamMgr_getAramAddr_();
            if (aramAddr != NULL) {
                bool result = mStreamAramMgr->deleteStreamAram((uintptr_t)aramAddr);
                JUT_ASSERT(105, result);
            }
            
            JKR_DELETE(stream);
        }
        i = next;
    }
}

void JAIStreamMgr::calc() {
    JSULink<JAIStream>* i;
    mParams.calc();
    for (i = mStreamList.getFirst(); i != NULL; i = i->getNext()) {
        i->getObject()->JAIStreamMgr_calc_();
    }
    freeDeadStream_();
}

void JAIStreamMgr::stop() {
    JSULink<JAIStream>* i;
    for (i = mStreamList.getFirst(); i != NULL; i = i->getNext()) {
        i->getObject()->stop();
    }
}

void JAIStreamMgr::stop(u32 fadeTime) {
    JSULink<JAIStream>* i;
    for (i = mStreamList.getFirst(); i != NULL; i = i->getNext()) {
        i->getObject()->stop(fadeTime);
    }
}

void JAIStreamMgr::stopSoundID(JAISoundID id) {
    JSULink<JAIStream>* i;
    for (i = mStreamList.getFirst(); i != NULL; i = i->getNext()) {
        if ((u32)i->getObject()->getID() == (u32)id) {
            i->getObject()->stop();
        }
    }
}

void JAIStreamMgr::mixOut() {
    JSULink<JAIStream>* i;
     for (i = mStreamList.getFirst(); i != NULL; i = i->getNext()) {
        i->getObject()->JAIStreamMgr_mixOut_(mParams.params_, mActivity);
    }
}

JAIStream* JAIStreamMgr::newStream_() {
    if (mStreamAramMgr == NULL) {
        JUT_WARN(229, "%s", "JAIStreamAramMgr must be set.\n");
        return NULL;
    } 

    JAIStream* stream = JKR_NEW JAIStream(this, field_0x6c);
    if (stream == NULL) {
        JUT_WARN(235, "%s", "JASPoolAllocObject::<JAIStream>::operator new failed .\n");
        return NULL;
    }

    mStreamList.append(stream);
    return stream;
}
