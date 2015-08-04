#ifndef FRAME_WORKER_H
#define FRAME_WORKER_H

/* Qt includes */
#include <QElapsedTimer>
#include <QObject>
#include <QMutex>
#include <QSharedPointer>
#include <QThread>
#include <QVector>

/* standard include */
#include <memory>

/* cuda_take includes */
#include "take_object.hpp"
#include "frame_c_meta.h"

/*! \file
 * \brief Communicates with the backend and connects public information between widgets.
 * \paragraph
 *
 * The framWorker class contains backend and analysis information that must be shared between classes in the GUI.
 * Structurally, frameWorker is a worker object tied to a QThread started in main. The main event loop in this object
 * may therefore be considered the function handleNewFrame() which periodically grabs a frame from the backend
 * and checks for asynchronous signals from the filters about the status of the data processing. A frame may still
 * be displayed even if analysis functions associated with it time out for the loop.
 * In general, the frameWorker is the only object with a copy of the take_object and should exclusively handle
 * communication with cuda_take.
 *
 * \author Noah Levy
 * \author JP Ryan
 */

class frameWorker : public QObject
{
    Q_OBJECT

    frame_c* std_dev_processing_frame = NULL;

    unsigned int dataHeight;
    unsigned int frHeight;
    unsigned int frWidth;

    unsigned long c = 0;
    unsigned long framecount_window = 50; //we measure elapsed time for the backend fps every 50 frames

    float * histogram_bins;

    bool crosshair_useDSF = false;
    bool doRun = true;

    QElapsedTimer deltaTimer;

public:
    explicit frameWorker(QObject *parent = 0);
    virtual ~frameWorker();

    take_object to;

    frame_c* curFrame  = NULL;
    frame_c* std_dev_frame = NULL;

    float delta;

    /* Used for frameview widgets */
    bool displayCross = true;

    /* Used for profile widgets */
    int horizLinesAvgd = 1;
    int vertLinesAvgd = 1;
    int crosshair_x = -1;
    int crosshair_y = -1;
    int crossStartRow = -1;
    int crossHeight = -1;
    int crossStartCol = -1;
    int crossWidth = -1;

    /*! Determines the default ceiling for all raw data based widgets based on the camera_t */
    int base_ceiling;

    camera_t camera_type();
    unsigned int getFrameHeight();
    unsigned int getDataHeight();
    unsigned int getFrameWidth();
    bool dsfMaskCollected();

signals:
    /*! \brief Calls to update the value of the backend FPS label */
    void updateFPS();

    /*! \brief Calls to render a new frame at the frontend.
     * \deprecated This signal may be deprecated in future versions. It is connected to the rendering slot in frameview_widget, but not emitted. */
    void newFrameAvailable();

    /*! \brief Calls to update the value of the Frames to Save label
     * \param n New number of frames left to save */
    void savingFrameNumChanged(unsigned int n);

    /*! \brief Closes the class event loop and calls to deallocate the workerThread. */
    void finished();

public slots:
    /*! \addtogroup renderfunc
     * @{ */
    void captureFrames();
    /*! @} */

    /*! \addtogroup maskfunc
     * @{ */
    void startCapturingDSFMask();
    void finishCapturingDSFMask();
    void toggleUseDSF(bool t);
    /*! @} */

    /*! \addtogroup savingfunc
     * @{ */
    void startSavingRawData(unsigned int framenum,QString name);
    void stopSavingRawData();
    /*! @} */

    void updateCrossDiplay(bool checked);
    void setStdDev_N(int newN);
    void updateDelta();
    void stop();

};


#endif // FRAME_WORKER_H
