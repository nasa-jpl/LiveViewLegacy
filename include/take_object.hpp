/*
 * takeobject.hpp
 *
 *  Created on: May 27, 2014
 *      Author: nlevy
 */

#ifndef TAKEOBJECT_HPP_
#define TAKEOBJECT_HPP_

/* No longer using C++11 stuff, now using Boost, they are supposed to have the same syntax
#include <thread>
#include <atomic>
#include <mutex>
*/
//#include <boost/atomic.hpp> //Atomic isn't in the boost library till 1.5.4, debian wheezy has 1.4.9 :(
#include <stdint.h>
#include <stdio.h>
#include <ostream>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

#include "std_dev_filter.cuh"
#include "chroma_translate_filter.cuh"
#include "dark_subtraction_filter.cuh"
#include "mean_filter.hpp"
#include "edtinc.h"
#include "camera_types.h"

static const bool CHECK_FOR_MISSED_FRAMES_6604A = true;
class take_object {
	PdvDev * pdv_p;
	boost::thread pdv_thread;
	boost::thread save_thread;
	bool pdv_thread_run;

	bool newFrameAvailable;
	uint16_t * raw_data_ptr;
	uint16_t * image_data_ptr;

	float * std_dev_data;
	float * dark_subtraction_data;
	uint32_t * std_dev_histogram_data;

	float * horizontal_mean_data;
	float * vertical_mean_data;
	float * fftReal_mean_data;




	unsigned int size;

	unsigned int channel;
	unsigned int numbufs;
	unsigned int frame_history_size;
	unsigned int filter_refresh_rate;

	int std_dev_filter_N;
	int lastfc;
	uint64_t count;
	chroma_translate_filter ctf;
	dark_subtraction_filter * dsf;
	std_dev_filter * sdvf;
	mean_filter * mf;

	unsigned int save_count;
	bool do_raw_save;

	bool saveFrameAvailable;

	uint16_t * raw_save_ptr;

	FILE * raw_save_file;

	u_char * dumb_ptr;
	unsigned int dataHeight;
	unsigned int frHeight;
	unsigned int frWidth;
public:
	take_object(int channel_num = 0, int number_of_buffers = 64, int fmsize = 1000, int filter_refresh_rate = 10);
	virtual ~take_object();
	void start();
	uint16_t * getImagePtr();
	uint16_t * getRawPtr();
	void waitForReadLock();
	void releaseReadLock();

	unsigned int getDataHeight();
	unsigned int getFrameHeight();
	unsigned int getFrameWidth();
	boost::mutex data_mutex;
	boost::unique_lock<boost::mutex> * read_lock;


	bool dsfMaskCollected;
	float * getStdDevData();

	float * getDarkSubtractedData();
	uint32_t * getHistogramData();
	std::vector<float> * getHistogramBins();

	float * getHorizontalMean();
	float * getVerticalMean();
	float * getRealFFTMagnitude();
	bool std_dev_ready();

	void startCapturingDSFMask();
	void finishCapturingDSFMask();
	void loadDSFMask(std::string file_name);


	void startSavingRaws(std::string, unsigned int );
	void stopSavingRaws();

	void setStdDev_N(int s);

	void doSave();
	camera_t cam_type;
	unsigned int save_framenum;

private:
	void pdv_loop();
	void savingLoop();


};

#endif /* TAKEOBJECT_HPP_ */
