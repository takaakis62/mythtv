// -*- Mode: c++ -*-

#ifndef _IMPORT_RECORDER_H_
#define _IMPORT_RECORDER_H_

#include <QMutex>

#include "dtvrecorder.h"
#include "tspacket.h"
#include "mpegstreamdata.h"
#include "DeviceReadBuffer.h"

struct AVFormatContext;
struct AVPacket;

/** \brief ImportRecorder imports files, creating a seek map and
 *         other stuff that MythTV likes to have for recording.
 *
 *  \note This currently only supports MPEG-TS files, but the
 *        plan is to support all files that ffmpeg does.
 */
class ImportRecorder : public DTVRecorder
{
  public:
    ImportRecorder(TVRec*);
    ~ImportRecorder();

    // RecorderBase
    void SetOptionsFromProfile(RecordingProfile *profile,
                               const QString &videodev,
                               const QString &audiodev,
                               const QString &vbidev);

    void StartRecording(void);
    void StopRecording(void);

    bool Open(void);
    void Close(void);

    void SetStreamData(void) {}

  private:
    int             _import_fd;

    QMutex          _end_of_recording_lock;
    QWaitCondition  _end_of_recording_wait;
};

#endif // _IMPORT_RECORDER_H_
