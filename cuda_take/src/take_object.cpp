#include "take_object.hpp"
#include "fft.hpp"


take_object::take_object(takeOptionsType options, int channel_num, int number_of_buffers,
                         int filter_refresh_rate, bool runStdDev)
{
    changeOptions(options);
    initialSetup(channel_num, number_of_buffers,
                 filter_refresh_rate, runStdDev);
}

take_object::take_object(int channel_num, int number_of_buffers,
                         int frf, bool runStdDev)
{
    warningMessage("Starting with assumed options. XIO disabled.");
    takeOptionsType options;
    options.xioCam = false;
    changeOptions(options);
    initialSetup(channel_num, number_of_buffers,
                 frf, runStdDev);
}

void take_object::initialSetup(int channel_num, int number_of_buffers,
                               int filter_refresh_rate, bool runStdDev)
{
    closing = false;
    this->channel = channel_num;
    this->numbufs = number_of_buffers;
    this->filter_refresh_rate = filter_refresh_rate;

    frame_ring_buffer = new frame_c[CPU_FRAME_BUFFER_SIZE];

    //For the filters
    dsfMaskCollected = false;
    this->std_dev_filter_N = 400;
    this->runStdDev = runStdDev;
    whichFFT = PLANE_MEAN;

    // for the overlay, zap everything to zero:
    this->lh_start = 0;
    this->lh_start = 0;
    this->lh_end = 0;
    this->cent_start = 0;
    this->cent_end = 0;
    this->rh_start = 0;
    this->rh_end = 0;

    //For the frame saving
    this->do_raw_save = false;
    savingData = false;
    continuousRecording = false;
    save_framenum = 0;
    save_count=0;
    save_num_avgs=1;
    saving_list.clear();
}

take_object::~take_object()
{
    closing = true;
    while(grabbing)
    {
        // wait here.
        usleep(1000);
    }
    if(pdv_thread_run != 0) {
        pdv_thread_run = 0;



        int dummy;
        if(pdv_p)
        {
            pdv_wait_last_image(pdv_p,&dummy); //Collect the last frame to avoid core dump
            pdv_close(pdv_p);
        }
        if(Camera)
        {
            delete Camera;
        }

#ifdef VERBOSE
        printf("about to delete filters!\n");
#endif

        delete dsf;
        delete sdvf;
    }

    delete[] frame_ring_buffer;

#ifdef RESET_GPUS
    printf("reseting GPUs!\n");
    int count;
    cudaGetDeviceCount(&count);
    for(int i = 0; i < count; i++) {
        printf("resetting GPU#%i",i);
        cudaSetDevice(i);
        cudaDeviceReset(); //Dump all the bad stuff from each of our GPUs.
    }
#endif
}

//public functions
void take_object::changeOptions(takeOptionsType optionsIn)
{
    this->options = optionsIn;

    if(optionsIn.xioDirSet)
    {
        if(optionsIn.xioDirectory == NULL)
        {
            errorMessage("Cannot have set directory that is null.");
            abort();
        } else {
             safeStringSet(options.xioDirectory, optionsIn.xioDirectory);
            }
   } else {
        // TODO: Wait safely for a directory, and do not try reading yet.
        errorMessage("xio directory not set. Cannot proceed to read from directory.");
    }

    statusMessage(std::string("Accepted startup options. Target FPS: ") + std::to_string(options.targetFPS));
    if(options.xioDirSet)
    {
        statusMessage(std::string("XIO directory: ") + *options.xioDirectory);
    }
}

void take_object::start()
{
    pdv_thread_run = 1;

    std::cout << "This version of cuda_take was compiled on " << __DATE__ << " at " << __TIME__ << " using gcc " << __GNUC__ << std::endl;
    std::cout << "The compilation was perfromed by " << UNAME << " @ " << HOST << std::endl;

    pthread_setname_np(pthread_self(), "TAKE");

    this->pdv_p = NULL;

    if(options.xioCam)
    {
        if(!options.heightWidthSet)
        {
            options.xioHeight = 480;
            options.xioWidth = 640;
            warningMessage("Warning: XIO Height and Width not specified. Assuming 640x480 geometry.");
        }
        frWidth = options.xioWidth;
        frHeight = options.xioHeight;
        dataHeight = options.xioHeight;
        size = frWidth * frHeight * sizeof(uint16_t);
        statusMessage("start() with XIO camera settings");
    } else {
        this->pdv_p = pdv_open_channel(EDT_INTERFACE,0,this->channel);
        if(pdv_p == NULL) {
            std::cerr << "Could not open device channel. Is one connected?" << std::endl;
            return;
        }
        size = pdv_get_dmasize(pdv_p); // this size is only used to determine the camera type
        // actual grabbing of the dimensions
        frWidth = pdv_get_width(pdv_p);
        dataHeight = pdv_get_height(pdv_p);

    }

    switch(size) {
    case 481*640*sizeof(uint16_t): cam_type = CL_6604A; break;
    case 285*640*sizeof(uint16_t): cam_type = CL_6604A; break;
    case 480*640*sizeof(uint16_t): cam_type = CL_6604B; pixRemap = true; break;
    default: cam_type = CL_6604B; pixRemap = true; break;
    }
	setup_filter(cam_type);
	if(pixRemap) {
		std::cout << "2s compliment filter ENABLED" << std::endl;
	} else {
		std::cout << "2s compliment filter DISABLED" << std::endl;
	}


    frHeight = cam_type == CL_6604A ? dataHeight - 1 : dataHeight;

#ifdef VERBOSE
    std::cout << "Camera Type: " << cam_type << ". Frame Width: " << frWidth << \
                 " Data Height: " << dataHeight << " Frame Height: " << frHeight << std::endl;
    std::cout << "About to start threads..." << std::endl;
#endif

    // Initialize the filters
    dsf = new dark_subtraction_filter(frWidth,frHeight);
    sdvf = new std_dev_filter(frWidth,frHeight);

    // Initial dimensions for calculating the mean that can be updated later
    meanStartRow = 0;
    meanStartCol = 0;
    meanHeight = frHeight;
    meanWidth = frWidth;


    numbufs = 16;
    int rtnval = 0;
    if(options.xioCam)
    {
        cam_thread_start_complete = false;
        statusMessage("Creating an XIO camera take_object.");
        prepareFileReading(); // make a camera
        statusMessage("Creating an XIO camera thread inside take_object.");
        cam_thread = boost::thread(&take_object::fileImageCopyLoop, this);
        cam_thread_handler = cam_thread.native_handle();
        pthread_setname_np(cam_thread_handler, "XIOCAM");
        statusMessage("Created thread.");
        while(!cam_thread_start_complete)
            usleep(100);
        // The idea is to hold off on doing anything else until some setup is finished.

        statusMessage("Creating XIO File reading thread reading_thread.");
        reading_thread = boost::thread(&take_object::fileImageReadingLoop, this);
        reading_thread_handler = reading_thread.native_handle();
        pthread_setname_np(reading_thread_handler, "READING");
        statusMessage("Done creating XIO File reading thread reading_thread.");

        char threadinfo[16];
        statusMessage("Thread Information: ");
        std::ostringstream info;

        pthread_getname_np(cam_thread_handler, threadinfo, 16);
        info << "Cam thread name: " << threadinfo;
        statusMessage(info);

        info.str("");

        pthread_getname_np(reading_thread_handler, threadinfo, 16);
        info << "Reading thread name: " << string(threadinfo);
        statusMessage(info);

        info.str("");

        pthread_getname_np(pthread_self(), threadinfo, 16);
        info << "Self thread name: " << string(threadinfo);
        statusMessage(info);


    } else {
        if(pdv_p != NULL)
            rtnval = pdv_multibuf(pdv_p,this->numbufs);
        if(rtnval != 0)
        {
            std::cout << "Error, could not initialize camera link multibuffer." << std::endl;
            std::cout << "Make sure the camera link driver is loaded and that the camera link port has been initialized using initcam." << std::endl;
        }


        pdv_start_images(pdv_p,numbufs); //Before looping, emit requests to fill the pdv ring buffer
        cam_thread = boost::thread(&take_object::pdv_loop, this);
        cam_thread_handler = cam_thread.native_handle();
        pthread_setname_np(cam_thread_handler, "PDVCAM");
        //usleep(350000);
        while(!cam_thread_start_complete) usleep(1); // Added by Michael Bernas 2016. Used to prevent thread error when starting without a camera
    }
}
void take_object::setInversion(bool checked, unsigned int factor)
{
    inverted = checked;
    invFactor = factor;
}
void take_object::paraPixRemap(bool checked )
{
    pixRemap = checked;
    std::cout << "2s Compliment Filter ";
    if(pixRemap) {
        std::cout << "ENABLED" << std::endl;
    } else {
        std::cout << "DISABLED" << std::endl;
    }
}
void take_object::startCapturingDSFMask()
{
    dsfMaskCollected = false;
    dsf->start_mask_collection();
}
void take_object::finishCapturingDSFMask()
{
    dsf->mask_mutex.lock();
    dsf->finish_mask_collection();
    dsf->mask_mutex.unlock();
    dsfMaskCollected = true;
}
void take_object::loadDSFMask(std::string file_name)
{
    float *mask_in = new float[frWidth*frHeight];
    FILE *pFile;
    unsigned long size = 0;
    pFile  = fopen(file_name.c_str(), "rb");
    if(pFile == NULL) std::cerr << "error opening raw file" << std::endl;
    else
    {
        fseek (pFile, 0, SEEK_END); // non-portable
        size = ftell(pFile);
        if(size != (frWidth*frHeight*sizeof(float)))
        {
            std::cerr << "Error: mask file does not match image size" << std::endl;
            fclose (pFile);
            return;
        }
        rewind(pFile);   // go back to beginning
        fread(mask_in,sizeof(float),frWidth * frHeight,pFile);
        fclose (pFile);
#ifdef VERBOSE
        std::cout << file_name << " read in "<< size << " bytes successfully " <<  std::endl;
#endif
    }
    dsf->load_mask(mask_in);
}
void take_object::setStdDev_N(int s)
{
    this->std_dev_filter_N = s;
}

void take_object::toggleStdDevCalculation(bool enabled)
{
    this->runStdDev = enabled;
}

void take_object::updateVertOverlayParams(int lh_start_in, int lh_end_in,
                                          int cent_start_in, int cent_end_in,
                                          int rh_start_in, int rh_end_in)
{
    this->lh_start = lh_start_in;
    this->lh_start = lh_start_in;
    this->lh_end = lh_end_in;
    this->cent_start = cent_start_in;
    this->cent_end = cent_end_in;
    this->rh_start = rh_start_in;
    this->rh_end = rh_end_in;

    /*
    // Debug, remove later:
    std::cout << "----- In take_object::updateVertOverlayParams\n";
    std::cout << "->lh_start:   " << lh_start <<   ", lh_end:   " << lh_end << std::endl;
    std::cout << "->rh_start:   " << rh_start <<   ", rh_end:   " << rh_end << std::endl;
    std::cout << "->cent_start: " << cent_start << ", cent_end: " << cent_end << std::endl;
    std::cout << "----- end take_object::updateVertOverlayParams -----\n";
    */
}

void take_object::updateVertRange(int br, int er)
{
    meanStartRow = br;
    meanHeight = er;
#ifdef VERBOSE
    std::cout << "meanStartRow: " << meanStartRow << " meanHeight: " << meanHeight << std::endl;
#endif
}
void take_object::updateHorizRange(int bc, int ec)
{
    meanStartCol = bc;
    meanWidth = ec;
#ifdef VERBOSE
    std::cout << "meanStartCol: " << meanStartCol << " meanWidth: " << meanWidth << std::endl;
#endif
}
void take_object::changeFFTtype(FFT_t t)
{
    whichFFT = t;
}
void take_object::startSavingRaws(std::string raw_file_name, unsigned int frames_to_save, unsigned int num_avgs_save)
{
    if(frames_to_save==0)
    {
        continuousRecording = true;
    } else {
        continuousRecording = false;
    }
    
    save_framenum.store(0, std::memory_order_seq_cst);
    save_count.store(0, std::memory_order_seq_cst);
#ifdef VERBOSE
    printf("ssr called\n");
#endif
    while(!saving_list.empty())
    {
#ifdef VERBOSE
        printf("Waiting for empty saving list...\n");
#endif
    }
    save_framenum.store(frames_to_save,std::memory_order_seq_cst);
    save_count.store(0, std::memory_order_seq_cst);
    save_num_avgs=num_avgs_save;
#ifdef VERBOSE
    printf("Begin frame save! @ %s\n", raw_file_name.c_str());
#endif

    saving_thread = boost::thread(&take_object::savingLoop,this,raw_file_name,num_avgs_save,frames_to_save);
}
void take_object::stopSavingRaws()
{
    continuousRecording = false;
    save_framenum.store(0,std::memory_order_relaxed);
    save_count.store(0,std::memory_order_relaxed);
    save_num_avgs=1;
#ifdef VERBOSE
    printf("Stop Saving Raws!");
#endif
}
/*void take_object::panicSave( std::string raw_file_name )
{
    while(!saving_list.empty());
    boost::thread(&take_object::saveFramesInBuffer,this);
    boost::thread(&take_object::savingLoop,this,raw_file_name);
}*/
unsigned int take_object::getDataHeight()
{
    return dataHeight;
}
unsigned int take_object::getFrameHeight()
{
    return frHeight;
}
unsigned int take_object::getFrameWidth()
{
    return frWidth;
}
bool take_object::std_dev_ready()
{
    return sdvf->outputReady();
}
std::vector<float> * take_object::getHistogramBins()
{
    return sdvf->getHistogramBins();
}
FFT_t take_object::getFFTtype()
{
    return whichFFT;
}

// private functions

void take_object::prepareFileReading()
{
    // Makes an XIO file reading camera

    if(Camera == NULL)
    {
        Camera = new XIOCamera(frWidth,
                               frHeight,
                               frHeight);
        if(Camera == NULL)
        {
            errorMessage("XIO Camera could not be created, was NULL.");
        } else {
            statusMessage(string("XIO Camera was made"));
        }
    } else {
        errorMessage("XIO Camera should be NULL at start but isn't");
    }

    bool cam_started = Camera->start();
    if(cam_started)
    {
        statusMessage("XIO Camera started.");
    } else {
        errorMessage("XIO Camera not started");
    }
}

void take_object::fileImageReadingLoop()
{
    // This thread makes the camera keep reading files
    // readLoop() runs readFile() inside.

    if(Camera)
    {
        statusMessage(std::string("Starting XIO Camera readLoop() function. Initial closing value: ") + std::string(closing?"true":"false"));
        // TODO: Come up with a switchable condition here
        // One-shot mode:
        while(!closing)
        {
            Camera->readLoop();
            //statusMessage("Completed readLoop(), pausing and then running again.");
            usleep(100000);
        }
        statusMessage("completed XIO Camera readLoop() while function. No more files can be read once completed. ");
    } else {
        errorMessage("XIO Camera is NULL, cannot readLoop().");
    }
}

void take_object::markFrameForChecking(uint16_t *frame)
{
    // This function overrides some data in the top three rows of the frame.
    // This is only to be used for debugging.

    // Pattern:
    // X 0 X 0 X X 0 X 0 X
    // X 0 X 0 X X 0 X 0 X
    // X 0 X 0 X X 0 X 0 X

    frame[0] = (uint16_t)0xffff;
    frame[1] = (uint16_t)0x0000;
    frame[2] = (uint16_t)0xffff;
    frame[3] = (uint16_t)0x0000;
    frame[4] = (uint16_t)0xffff;
    frame[5] = (uint16_t)0xffff;
    frame[6] = (uint16_t)0x0000;
    frame[7] = (uint16_t)0xffff;
    frame[8] = (uint16_t)0x0000;
    frame[9] = (uint16_t)0xffff;

    frame[0+640] = (uint16_t)0xffff;
    frame[1+640] = (uint16_t)0x0000;
    frame[2+640] = (uint16_t)0xffff;
    frame[3+640] = (uint16_t)0x0000;
    frame[4+640] = (uint16_t)0xffff;
    frame[5+640] = (uint16_t)0xffff;
    frame[6+640] = (uint16_t)0x0000;
    frame[7+640] = (uint16_t)0xffff;
    frame[8+640] = (uint16_t)0x0000;
    frame[9+640] = (uint16_t)0xffff;

    frame[0+640+640] = (uint16_t)0xffff;
    frame[1+640+640] = (uint16_t)0x0000;
    frame[2+640+640] = (uint16_t)0xffff;
    frame[3+640+640] = (uint16_t)0x0000;
    frame[4+640+640] = (uint16_t)0xffff;
    frame[5+640+640] = (uint16_t)0xffff;
    frame[6+640+640] = (uint16_t)0x0000;
    frame[7+640+640] = (uint16_t)0xffff;
    frame[8+640+640] = (uint16_t)0x0000;
    frame[9+640+640] = (uint16_t)0xffff;
}

bool take_object::checkFrame(uint16_t* Frame)
{
    bool ok = true;
    ok &= Frame[1] == (uint16_t)0x0000;
    ok &= Frame[2] == (uint16_t)0xffff;
    ok &= Frame[3] == (uint16_t)0x0000;
    ok &= Frame[4] == (uint16_t)0xffff;
    ok &= Frame[5] == (uint16_t)0xffff;
    ok &= Frame[6] == (uint16_t)0x0000;
    ok &= Frame[7] == (uint16_t)0xffff;
    ok &= Frame[8] == (uint16_t)0x0000;
    ok &= Frame[9] == (uint16_t)0xffff;
    statusMessage(std::string("Frame check result (1 of 3): ") + std::string(ok?"GOOD":"BAD"));

    // Test for bad data:
    // Frame[4+640] = (uint16_t)0xABCD; // intentional

    ok &= Frame[0+640] == (uint16_t)0xffff;
    ok &= Frame[1+640] == (uint16_t)0x0000;
    ok &= Frame[2+640] == (uint16_t)0xffff;
    ok &= Frame[3+640] == (uint16_t)0x0000;
    ok &= Frame[4+640] == (uint16_t)0xffff;
    ok &= Frame[5+640] == (uint16_t)0xffff;
    ok &= Frame[6+640] == (uint16_t)0x0000;
    ok &= Frame[7+640] == (uint16_t)0xffff;
    ok &= Frame[8+640] == (uint16_t)0x0000;
    ok &= Frame[9+640] == (uint16_t)0xffff;
    statusMessage(std::string("Frame check result: (2 of 3): ") + std::string(ok?"GOOD":"BAD"));

    ok &= Frame[0+640+640] == (uint16_t)0xffff;
    ok &= Frame[1+640+640] == (uint16_t)0x0000;
    ok &= Frame[2+640+640] == (uint16_t)0xffff;
    ok &= Frame[3+640+640] == (uint16_t)0x0000;
    ok &= Frame[4+640+640] == (uint16_t)0xffff;
    ok &= Frame[5+640+640] == (uint16_t)0xffff;
    ok &= Frame[6+640+640] == (uint16_t)0x0000;
    ok &= Frame[7+640+640] == (uint16_t)0xffff;
    ok &= Frame[8+640+640] == (uint16_t)0x0000;
    ok &= Frame[9+640+640] == (uint16_t)0xffff;

    statusMessage(std::string("Frame check result: (3 of 3): ") + std::string(ok?"GOOD":"BAD"));

    return ok;
}

void take_object::clearAllRingBuffer()
{
    frame_c *curFrame = NULL;
    uint16_t *zeroFrame = NULL;
    zeroFrame = (uint16_t*)calloc(frWidth*dataHeight , sizeof(uint16_t));
    if(zeroFrame == NULL)
    {
        errorMessage("Zero-frame could not be established.");
        abort();
    }

    for(size_t f=0; f < CPU_FRAME_BUFFER_SIZE; f++)
    {
        curFrame = &frame_ring_buffer[f];
        curFrame->reset();
        memcpy(curFrame->raw_data_ptr,zeroFrame,frWidth*dataHeight);
    }
    statusMessage("Done zero-setting memory in frame_ring_buffer");
}

void take_object::fileImageCopyLoop()
{
    // This thread copies data from the XIO Camera's buffer
    // and into curFrane of take_object. It is the "consumer"
    // thread in a way.

    uint16_t *zeroFrame = NULL;
    zeroFrame = (uint16_t*)calloc(frWidth*dataHeight , sizeof(uint16_t));
    if(zeroFrame == NULL)
    {
        errorMessage("Zero-frame could not be established. You may be out of memory.");
        abort();
    }

    // Verify our frame data stability:
    markFrameForChecking(zeroFrame); // adds special data to the frame which can be checked for later.

    bool goodResult = checkFrame(zeroFrame);
    if(goodResult == false)
    {
        errorMessage("ERROR, BAD data detected");
        abort();
    } else {
        statusMessage("Initial data check passed.");
    }
    // End verification.

    bool hasBeenNull = false;

    if(Camera)
    {
        count = 0;
        uint16_t framecount = 1;
        uint16_t last_framecount = 0;
        (void)last_framecount; // use count

        mean_filter * mf = new mean_filter(curFrame,count,meanStartCol,meanWidth,\
                                           meanStartRow,meanHeight,frWidth,useDSF,\
                                           whichFFT, lh_start, lh_end,\
                                           cent_start, cent_end,\
                                           rh_start, rh_end);

        if(options.targetFPS == 0.0)
            options.targetFPS = 100.0;

        float deltaT_micros = 1000000.0 / options.targetFPS;
        int measuredDelta_micros = 0;
        fileReadingLoopRun = true;

        std::chrono::steady_clock::time_point begintp;
        std::chrono::steady_clock::time_point endtp;

        while(fileReadingLoopRun && (!closing))
        {
            begintp = std::chrono::steady_clock::now();

            grabbing = true;
            curFrame = &frame_ring_buffer[count % CPU_FRAME_BUFFER_SIZE];
            curFrame->reset();

            if(closing)
            {
                fileReadingLoopRun = false;
                break;
            } else {
                // start image collection on the camera

            }
            cam_thread_start_complete=true;

            uint16_t* temp_frame = Camera->getFrame(); // this is where the FPS should be set

            if(temp_frame)
            {
                //markFrameForChecking(temp_frame);
                memcpy(curFrame->raw_data_ptr,temp_frame,frWidth*dataHeight);
                //goodResult = checkFrame(curFrame->raw_data_ptr);
                if(hasBeenNull)
                {
                    // This is here to catch if the frame is NULL, meaning
                    // data has finished, but then the frame goes back to
                    // being valid data.
                    statusMessage("Note: frame was NULL, but is not anymore. ");
                    hasBeenNull = false;
                    //abort(); // test, remove later.
                }
            } else {
                if(!hasBeenNull)
                {
                    // What if we don't do it?
                    // clearAllRingBuffer();
                }

                //hasBeenNull = true;
                //errorMessage("Frame was NULL!");
                memcpy(curFrame->raw_data_ptr,zeroFrame,frWidth*dataHeight);

                // Check frame intregity:
                //goodResult = checkFrame(curFrame->raw_data_ptr);
//                if(!goodResult)
//                {
//                    errorMessage("Frame failed check");
//                    abort();
//                }

            }

            // From here on out, the code should be
            // very similar to the EDT frame grabber code.

            if(true)
            {
            if(pixRemap)
                apply_chroma_translate_filter(curFrame->raw_data_ptr);
            if(cam_type == CL_6604A)
                curFrame->image_data_ptr = curFrame->raw_data_ptr + frWidth;
            else
                curFrame->image_data_ptr = curFrame->raw_data_ptr;
            if(inverted)
            { // record the data from high to low. Store the pixel buffer in INVERTED order from the camera link
                for(uint i = 0; i < frHeight*frWidth; i++ )
                    curFrame->image_data_ptr[i] = invFactor - curFrame->image_data_ptr[i];
            }
            } else {
                curFrame->image_data_ptr = curFrame->raw_data_ptr;
            }

            // Calculating the filters for this frame
            if(runStdDev)
            {
                sdvf->update_GPU_buffer(curFrame,std_dev_filter_N);
            }
            dsf->update(curFrame->raw_data_ptr,curFrame->dark_subtracted_data);
            mf->update(curFrame,count,meanStartCol,meanWidth,\
                       meanStartRow,meanHeight,frWidth,useDSF,\
                       whichFFT, lh_start, lh_end,\
                                               cent_start, cent_end,\
                                               rh_start, rh_end);

            mf->start_mean();

            if((save_framenum > 0) || continuousRecording)
            {
                uint16_t * raw_copy = new uint16_t[frWidth*dataHeight];
                memcpy(raw_copy,curFrame->raw_data_ptr,frWidth*dataHeight*sizeof(uint16_t));
                saving_list.push_front(raw_copy);
                save_framenum--;
            }

            framecount = *(curFrame->raw_data_ptr + 160); // The framecount is stored 160 bytes offset from the beginning of the data
            /*
            if(CHECK_FOR_MISSED_FRAMES_6604A && cam_type == CL_6604A)
            {
                if( (framecount - 1 != last_framecount) && (last_framecount != UINT16_MAX) )
                {
                    std::cerr << "WARNING: MISSED FRAME " << framecount << std::endl;
                }
            }
            */


//            goodResult = checkFrame(curFrame->raw_data_ptr);
//            if(!goodResult)
//            {
//                errorMessage("Frame failed late check");
//                errorMessage(std::string("pixel 0: ") + std::to_string((int)curFrame->raw_data_ptr[0]));
//                errorMessage(std::string("pixel 1: ") + std::to_string((int)curFrame->raw_data_ptr[1]));

//                abort();
//            }


            last_framecount = framecount;
            count++;
            grabbing = false;
            if(closing)
            {
                fileReadingLoopRun = false;
                break;
            }


            // Forced FPS
            endtp = std::chrono::steady_clock::now();
            measuredDelta_micros = std::chrono::duration_cast<std::chrono::microseconds>(endtp-begintp).count();
            if(measuredDelta_micros < deltaT_micros)
            {
                // wait
                //statusMessage(std::string("Waiting additional ") + std::to_string(deltaT_micros - measuredDelta_micros) + std::string(" microseconds."));
                usleep(deltaT_micros - measuredDelta_micros);
            } else {
                //warningMessage("Cannot guarentee requested frame rate. Frame rate is too fast or computation is too slow.");
                //warningMessage(std::string("Requested deltaT: ") + std::to_string(deltaT_micros) + std::string(", measured delta microseconds: ") + std::to_string(measuredDelta_micros));
            }
            // if elapsed time < required time
            // wait delta.
        }
        statusMessage("Done providing frames");
    } else {
        errorMessage("Camera was NULL!");
        abort();
    }
    if(zeroFrame != NULL)
        free(zeroFrame);
}

void take_object::setReadDirectory(const char *directory)
{
    if(directory == NULL)
    {
        errorMessage("directory is empty string or NULL, cannot set directory.");
        return;
    }

    if(Camera == NULL)
    {
        errorMessage("Camera is NULL! Cannot set directory (yet).");
        return;
    }

    if(sizeof(directory) != 0)
    {
        statusMessage(string("Setting directory to: ") + directory);
        Camera->setDir(directory);
    } else {
        errorMessage("Cannot set directory to zero-length string.");
    }
}

void take_object::pdv_loop() //Producer Thread (pdv_thread)
{
	count = 0;

    uint16_t framecount = 1;
    uint16_t last_framecount = 0;
#ifdef EDT
	unsigned char* wait_ptr;
#endif


    mean_filter * mf = new mean_filter(curFrame,count,meanStartCol,meanWidth,\
                                       meanStartRow,meanHeight,frWidth,useDSF,\
                                       whichFFT, lh_start, lh_end,\
                                       cent_start, cent_end,\
                                       rh_start, rh_end);

    while(pdv_thread_run == 1)
    {	
        grabbing = true;
        curFrame = &frame_ring_buffer[count % CPU_FRAME_BUFFER_SIZE];
        curFrame->reset();
        if(closing)
        {
            pdv_thread_run = 0;
            break;

        } else {
            pdv_start_image(pdv_p); //Start another
            // Have seen Segmentation faults here on closing liveview:
            if(!closing) wait_ptr = pdv_wait_image(pdv_p);
        }
        cam_thread_start_complete=true;

        /* In this section of the code, after we have copied the memory from the camera link
         * buffer into the raw_data_ptr, we will check various parameters to see if we need to
         * modify the data based on our hardware.
         *
         * First, the data is stored differently depending on the type of camera, 6604A or B.
         *
         * Second, we may have to apply a filter to pixels which remaps the image based on the
         * way information is sent by some detectors.
         *
         * Third, we may need to invert the data range if a cable is inverting the magnitudes
         * that arrive from the ADC. This feature is also modified from the preference window.
         */
        memcpy(curFrame->raw_data_ptr,wait_ptr,frWidth*dataHeight*sizeof(uint16_t));
        if(pixRemap)
            apply_chroma_translate_filter(curFrame->raw_data_ptr);
        if(cam_type == CL_6604A)
            curFrame->image_data_ptr = curFrame->raw_data_ptr + frWidth;
        else
            curFrame->image_data_ptr = curFrame->raw_data_ptr;
        if(inverted)
        { // record the data from high to low. Store the pixel buffer in INVERTED order from the camera link
            for(uint i = 0; i < frHeight*frWidth; i++ )
                curFrame->image_data_ptr[i] = invFactor - curFrame->image_data_ptr[i];
        }

        // Calculating the filters for this frame
        if(runStdDev)
        {
            sdvf->update_GPU_buffer(curFrame,std_dev_filter_N);
        }
        dsf->update(curFrame->raw_data_ptr,curFrame->dark_subtracted_data);
        mf->update(curFrame,count,meanStartCol,meanWidth,\
                   meanStartRow,meanHeight,frWidth,useDSF,\
                   whichFFT, lh_start, lh_end,\
                                           cent_start, cent_end,\
                                           rh_start, rh_end);

        mf->start_mean();

        if((save_framenum > 0) || continuousRecording)
        {
            uint16_t * raw_copy = new uint16_t[frWidth*dataHeight];
            memcpy(raw_copy,curFrame->raw_data_ptr,frWidth*dataHeight*sizeof(uint16_t));
            saving_list.push_front(raw_copy);
            save_framenum--;
        }

#ifdef EDT

#endif

        framecount = *(curFrame->raw_data_ptr + 160); // The framecount is stored 160 bytes offset from the beginning of the data
        if(CHECK_FOR_MISSED_FRAMES_6604A && cam_type == CL_6604A)
        {
            if( (framecount - 1 != last_framecount) && (last_framecount != UINT16_MAX) )
            {
                std::cerr << "WARNING: MISSED FRAME " << framecount << std::endl;
            }
        }
        last_framecount = framecount;
        count++;
        grabbing = false;
        if(closing)
        {
            pdv_thread_run = 0;
            break;
        }
    }
}
void take_object::savingLoop(std::string fname, unsigned int num_avgs, unsigned int num_frames) 
//Frame Save Thread (saving_thread)
{
    if(savingData)
    {
        warningMessage("Saving loop hit but already saving data!");
        return;
    } else {
        savingData = true;
    }

    savingMutex.lock();

    if(fname.find(".")!=std::string::npos)
    {
        fname.replace(fname.find("."),std::string::npos,".raw");
    }
    else
    {
        fname+=".raw";
    }
    std::string hdr_fname = fname.substr(0,fname.size()-3) + "hdr";
    FILE * file_target = fopen(fname.c_str(), "wb");
    int sv_count = 0;

    while(  (save_framenum != 0 || continuousRecording)    ||  !saving_list.empty())
    {
        if(!saving_list.empty())
        {
            if(num_avgs == 1)
            {
                // This is the "normal" behavior.
                uint16_t * data = saving_list.back();
                saving_list.pop_back();
                fwrite(data,sizeof(uint16_t),frWidth*dataHeight,file_target); //It is ok if this blocks
                delete[] data;
                sv_count++;
                if(sv_count == 1) {
                    save_count.store(1, std::memory_order_seq_cst);
                }
                else {
                    save_count++;
                }
            }
            else if(saving_list.size() >= num_avgs && num_avgs != 1)
            {
                float * data = new float[frWidth*dataHeight];
                for(unsigned int i2 = 0; i2 < num_avgs; i2++)
                {
                    uint16_t * data2 = saving_list.back();
                    saving_list.pop_back();
                    if(i2 == 0)
                    {
                        for(unsigned int i = 0; i < frWidth*dataHeight; i++)
                        {
                            data[i] = (float)data2[i];
                        }
                    }
                    else if(i2 == num_avgs-1)
                    {
                        for(unsigned int i = 0; i < frWidth*dataHeight; i++)
                        {
                            data[i] = (data[i] + (float)data2[i])/num_avgs;
                        }
                    }
                    else
                    {
                        for(unsigned int i = 0; i < frWidth*dataHeight; i++)
                        {
                            data[i] += (float)data2[i];
                        }
                    }
                    delete[] data2;
                }
                fwrite(data,sizeof(float),frWidth*dataHeight,file_target); //It is ok if this blocks
                delete[] data;
                sv_count++;
                if(sv_count == 1) {
                    save_count.store(1, std::memory_order_seq_cst);
                }
                else {
                    save_count++;
                }
                //std::cout << "save_count: " << std::to_string(save_count) << "\n";
                //std::cout << "list size: " << std::to_string(saving_list.size() ) << "\n";
                //std::cout << "save_framenum: " << std::to_string(save_framenum) << "\n";
            }
            else if(save_framenum == 0 && saving_list.size() < num_avgs)
            {
                saving_list.erase(saving_list.begin(),saving_list.end());
            }
            else
            {
                //We're waiting for data to get added to the list...
                usleep(250);
            }
        }
        else
        {
            //We're waiting for data to get added to the list...
            usleep(250);
        }
    }
    //We're done!
    fclose(file_target);
    std::string hdr_text = "ENVI\ndescription = {LIVEVIEW raw export file, " + std::to_string(num_avgs) + " frame mean per grab}\n";
    hdr_text= hdr_text + "samples = " + std::to_string(frWidth) +"\n";
    hdr_text= hdr_text + "lines   = " + std::to_string(sv_count) +"\n";
    hdr_text= hdr_text + "bands   = " + std::to_string(dataHeight) +"\n";
    hdr_text+= "header offset = 0\n";
    hdr_text+= "file type = ENVI Standard\n";
    if(num_avgs != 1)
    {
        hdr_text+= "data type = 4\n";
    }
    else
    {
        hdr_text+= "data type = 12\n";
    }
    hdr_text+= "interleave = bil\n";
    hdr_text+="sensor type = Unknown\n";
    hdr_text+= "byte order = 0\n";
    hdr_text+= "wavelength units = Unknown\n";
    //std::cout << hdr_text;
    std::ofstream hdr_target(hdr_fname);
    hdr_target << hdr_text;
    hdr_target.close();
    // What does this usleep do? --EHL
    if(sv_count == 1)
        usleep(500000);
    save_count.store(0, std::memory_order_seq_cst);
    //std::cout << "save_count: " << std::to_string(save_count) << "\n";
    //std::cout << "list size: " << std::to_string(saving_list.size() ) << "\n";
    //std::cout << "save_framenum: " << std::to_string(save_framenum) << "\n";
    statusMessage("Saving complete.");
    savingMutex.unlock();
    savingData = false;
}

void take_object::errorMessage(const char *message)
{
    std::cout << "take_object: ERROR: " << message << std::endl;
}

void take_object::warningMessage(const char *message)
{
    std::cout << "take_object: WARNING: " << message << std::endl;
}

void take_object::statusMessage(const char *message)
{
    std::cout << "take_object: STATUS: " << message << std::endl;
}

void take_object::errorMessage(const string message)
{
    std::cout << "take_object: ERROR: " << message << std::endl;
}

void take_object::warningMessage(const string message)
{
    std::cout << "take_object: WARNING: " << message << std::endl;
}

void take_object::statusMessage(const string message)
{
    std::cout << "take_object: STATUS: " << message << std::endl;
}

void take_object::statusMessage(std::ostringstream &message)
{
    std::cout << "take_object: STATUS: " << message.str() << std::endl;
}
