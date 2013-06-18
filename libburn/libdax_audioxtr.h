
/* libdax_audioxtr
   Audio track data extraction facility of libdax and libburn.
   Copyright (C) 2006 Thomas Schmitt <scdbackup@gmx.net>, provided under GPLv2+
*/

#ifndef LIBDAX_AUDIOXTR_H_INCLUDED
#define LIBDAX_AUDIOXTR_H_INCLUDED 1


/* Normally this public API is defined in <libburn/libburn.h>
   Macro LIBDAX_AUDIOXTR_H_PUBLIC enables the definition for programs
   which only include this file.
*/
#ifdef LIBDAX_AUDIOXTR_H_PUBLIC

                            /* Public Macros */

/* Maximum size for address paths and fmt_info strings */
#define LIBDAX_AUDIOXTR_STRLEN 4096


                          /* Public Opaque Handles */

/** Extractor object encapsulating intermediate states of extraction.
    The clients of libdax_audioxtr shall only allocate pointers to this
    struct and get a storage object via libdax_audioxtr_new().
    Appropriate initial value for the pointer is NULL.
*/
struct libdax_audioxtr;


                            /* Public Functions */

               /* Calls initiated from inside libdax/libburn */


     /* Calls from applications (to be forwarded by libdax/libburn) */


/** Open an audio file, check wether suitable, create extractor object.
    @param xtr Opaque handle to extractor. Gets attached extractor object.
    @param path Address of the audio file to extract. "-" is stdin (but might
                be not suitable for all futurely supported formats).
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return >0 success
             0 unsuitable format
            -1 severe error
            -2 path not found
*/
int libdax_audioxtr_new(struct libdax_audioxtr **xtr, char *path, int flag);


/** Obtain identification parameters of opened audio source.
    @param xtr Opaque handle to extractor
    @param fmt Gets pointed to the audio file format id text: ".wav" , ".au"
    @param fmt_info Gets pointed to a format info text telling parameters
    @param num_channels     e.g. 1=mono, 2=stereo, etc
    @param sample_rate      e.g. 11025, 44100 
    @param bits_per_sample  e.g. 8= 8 bits per sample, 16= 16 bits ...
    @param msb_first Byte order of samples: 0=Intel 1=Motorola 
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return >0 success, <=0 failure
*/
int libdax_audioxtr_get_id(struct libdax_audioxtr *xtr,
                           char **fmt, char **fmt_info,
                           int *num_channels, int *sample_rate,
                           int *bits_per_sample, int *msb_first, int flag);


/** Obtain a prediction about the extracted size based on internal information
    of the formatted file.
    @param xtr Opaque handle to extractor
    @param size Gets filled with the predicted size
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return 1 prediction was possible , 0 no prediction could be made
*/
int libdax_audioxtr_get_size(struct libdax_audioxtr *o, off_t *size, int flag);


/** Obtain next buffer full of extracted data in desired format (only raw audio
    for now).
    @param xtr Opaque handle to extractor
    @param buffer Gets filled with extracted data
    @param buffer_size Maximum number of bytes to be filled into buffer
    @param flag Bitfield for control purposes
                bit0= do not stop at predicted end of data
    @return >0 number of valid buffer bytes,
             0 End of file
            -1 operating system reports error
            -2 usage error by application
*/
int libdax_audioxtr_read(struct libdax_audioxtr *xtr,
                         char buffer[], int buffer_size, int flag);


/** Try to obtain a file descriptor which will deliver extracted data
    to normal calls of read(2). This may fail because the format is
    unsuitable for that, but ".wav" is ok. If this call succeeds the xtr
    object will have forgotten its file descriptor and libdax_audioxtr_read()
    will return a usage error. One may use *fd after libdax_audioxtr_destroy()
    and will have to close it via close(2) when done with it.
    @param xtr Opaque handle to extractor
    @param fd Eventually returns the file descriptor number
    @param flag Bitfield for control purposes
                bit0= do not dup(2) and close(2) but hand out original fd
    @return 1 success, 0 cannot hand out fd , -1 severe error
*/
int libdax_audioxtr_detach_fd(struct libdax_audioxtr *o, int *fd, int flag);


/** Clean up after extraction and destroy extractor object.
    @param xtr Opaque handle to extractor, *xtr is allowed to be NULL,
               *xtr is set to NULL by this function
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return 1 = destroyed object, 0 = was already destroyed
*/
int libdax_audioxtr_destroy(struct libdax_audioxtr **xtr, int flag);

#endif /* LIBDAX_AUDIOXTR_H_PUBLIC */


#ifdef LIBDAX_AUDIOXTR________________


-- place documentation text here ---


#endif /* LIBDAX_AUDIOXTR_________________ */



/*
  *Never* set this macro outside libdax_audioxtr.c !
  The entrails of this facility are not to be seen by
  the other library components or the applications.
*/
#ifdef LIBDAX_AUDIOXTR_H_INTERNAL

                       /* Internal Structures */

/** Extractor object encapsulating intermediate states of extraction */
struct libdax_audioxtr {

 /* Source of the encoded audio data */
 char path[LIBDAX_AUDIOXTR_STRLEN];

 /* File descriptor to path. Anything else than 0 must be lseek-able */
 int fd;

 /* Format identifier. E.g. ".wav" */
 char fmt[80];

 /* Format parameter info text */
 char fmt_info[LIBDAX_AUDIOXTR_STRLEN];

 /* 1= mono, 2= stereo, etc. */
 int num_channels;

 /* 8000, 44100, etc. */
 int sample_rate;

 /* 8 bits = 8, 16 bits = 16, etc. */
 int bits_per_sample;

 /* Byte order of samples: 0=Intel 1=Motorola */
 int msb_first;

 /* Number of bytes to extract (0= unknown/unlimited) */
 off_t data_size;

 /* Number of extracted data bytes */
 off_t extract_count;


 /* Format dependent parameters */

 /* MS WAVE Format */
 /* info used: http://ccrma.stanford.edu/courses/422/projects/WaveFormat/ */

 /* == NumSamples * NumChannels * BitsPerSample/8
    This is the number of bytes in the data. */
 unsigned wav_subchunk2_size;


 /* Sun Audio, .au */
 /* info used: http://www.opengroup.org/public/pubs/external/auformat.html */

 /* Number of bytes in non-payload header part */
 unsigned au_data_location;

 /* Number of payload bytes or 0xffffffff */
 unsigned au_data_size;

};


                             /* Internal Functions */

/** Open the audio source pointed to by .path and evaluate suitability.
    @return -1 failure to open, 0 unsuitable format, 1 success
*/
static int libdax_audioxtr_open(struct libdax_audioxtr *o, int flag);


/** Identify format and evaluate suitability.
    @return 0 unsuitable format, 1 format is suitable
*/
static int libdax_audioxtr_identify(struct libdax_audioxtr *o, int flag);

/** Specialized identifier for .wav */
static int libdax_audioxtr_identify_wav(struct libdax_audioxtr *o, int flag);
/** Specialized identifier for .au */
static int libdax_audioxtr_identify_au(struct libdax_audioxtr *o, int flag);


/** Convert a byte string into a number (currently only little endian)
    @param flag Bitfield for control purposes
                bit0=msb_first
    @return The resulting number
*/
static unsigned libdax_audioxtr_to_int(struct libdax_audioxtr *o,
                                      unsigned char *bytes, int len, int flag);


/** Prepare for reading of first buffer.
    @return 0 error, 1 success
*/
static int libdax_audioxtr_init_reading(struct libdax_audioxtr *o, int flag);



#endif /* LIBDAX_AUDIOXTR_H_INTERNAL */


#endif /* ! LIBDAX_AUDIOXTR_H_INCLUDED */

