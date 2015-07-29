/*
 * dark_subtraction_filter.cuh
 *
 *  Created on: May 22, 2014
 *      Author: nlevy
 */

#ifndef DARK_SUBTRACTION_FILTER_CUH_
#define DARK_SUBTRACTION_FILTER_CUH_

#include <stdint.h>
#include "edtinc.h"
#include "constants.h"


class dark_subtraction_filter
{
public:
	dark_subtraction_filter() {};//Useless defauklt constructor
	dark_subtraction_filter(int nWidth, int nHeight);
	virtual ~dark_subtraction_filter();
    void update_dark_subtraction(uint16_t* pic_in, float* pic_out);
    void static_dark_subtract(unsigned int* pic_in, float* pic_out);
	float * wait_dark_subtraction();
	void start_mask_collection();
	uint32_t update_mask_collection(uint16_t * pic_in);
	void update(uint16_t * pic_in, float * pic_out);

    void finish_mask_collection();
	void load_mask(float * mask_arr);
	float * get_mask();
private:
	bool mask_collected;
	//boost::shared_array<float> picture_out;
	unsigned int width;
	unsigned int height;
	unsigned int averaged_samples;

	float mask[MAX_SIZE];

};

#endif /* DARK_SUBTRACTION_FILTER_CUH_ */
