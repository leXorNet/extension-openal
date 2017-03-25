#include "OpenAL.h"
/*
 * Struct that holds the RIFF data of the Wave file.
 * The RIFF data is the meta data information that holds,
 * the ID, size and format of the wave file
 */
struct RIFF_Header {
	char chunkID[4];
	long chunkSize;//size not including chunkSize or chunkID
	char format[4];
};

/*
 * Struct to hold fmt subchunk data for WAVE files.
 */
struct WAVE_Format {
	char subChunkID[4];
	long subChunkSize;
	short audioFormat;
	short numChannels;
	long sampleRate;
	long byteRate;
	short blockAlign;
	short bitsPerSample;
};

/*
* Struct to hold the data of the wave file
*/
struct WAVE_Data {
	char subChunkID[4]; //should contain the word data
	long subChunk2Size; //Stores the size of the data block
};

/*
 * Load wave file function. No need for ALUT with this
 */
 // TODO: Make it work for WAV files with metadata.
static ALuint loadWavFile(dmBuffer::HBuffer* sourceBuffer) {
	uint8_t* sourcedata = 0;
	uint32_t datasize = 0;
	dmBuffer::GetBytes(*sourceBuffer, (void**)&sourcedata, &datasize);

	//Local Declarations
	WAVE_Format wave_format;
	RIFF_Header riff_header;
	WAVE_Data wave_data;
	unsigned char* data;

	// Read in the first chunk into the struct
	memcpy(&riff_header, sourcedata, sizeof(RIFF_Header));

	//check for RIFF and WAVE tag in memeory
	if ((strncmp(riff_header.chunkID, "RIFF", 4) != 0) && (strncmp(riff_header.chunkID, "WAVE", 4) != 0)) {
		dmLogError("Invalid RIFF or WAVE header");
		return 0;
	}

	//Read in the 2nd chunk for the wave info
	uint8_t* cursor = sourcedata + sizeof(RIFF_Header);
	memcpy(&wave_format, cursor, sizeof(WAVE_Format));

	//check for fmt tag in memory
	if (strncmp(wave_format.subChunkID, "fmt ", 4) != 0) {
		dmLogError("Invalid fmt header");
		return 0;
	}

	int extraSize = 0;
	//check for extra parameters;
	if (wave_format.subChunkSize > 16) {
		extraSize = sizeof(short);
	}

	//Read in the the last byte of data before the sound file
	cursor += sizeof(WAVE_Format) + extraSize;
	memcpy(&wave_data, cursor, sizeof(WAVE_Data));

	//check for data tag in memory
	if (strncmp(wave_data.subChunkID, "data", 4) != 0) {
		dmLogError("Invalid fmt header");
		return 0;
	}

	//Allocate memory for data
	data = new unsigned char[wave_data.subChunk2Size];

	// Read in the sound data into the soundData variable
	cursor += sizeof(WAVE_Data);
	memcpy(data, cursor, wave_data.subChunk2Size);

	//Now we set the variables that we passed in with the
	//data from the structs
	ALsizei size = wave_data.subChunk2Size;
	ALsizei frequency = wave_format.sampleRate;
	ALenum format = AL_FORMAT_MONO8;
	//The format is worked out by looking at the number of
	//channels and the bits per sample.
	if (wave_format.numChannels == 1) {
		if (wave_format.bitsPerSample == 8) {
			format = AL_FORMAT_MONO8;
		} else if (wave_format.bitsPerSample == 16) {
			format = AL_FORMAT_MONO16;
		}
	} else if (wave_format.numChannels == 2) {
		if (wave_format.bitsPerSample == 8) {
			format = AL_FORMAT_STEREO8;
		} else if (wave_format.bitsPerSample == 16) {
			format = AL_FORMAT_STEREO16;
		}
	}
	//create our openAL buffer and check for success
	ALuint buffer = 0;
	alGenBuffers(1, &buffer);

	//now we put our data into the openAL buffer and
	//check for success
	alBufferData(buffer, format, (void*)data, size, frequency);
	delete data;

	/* Check if an error occured, and clean up if so. */
	ALenum err = alGetError();
	if(err != AL_NO_ERROR) {
		dmLogError("OpenAL Error: %s\n", alGetString(err));
		if(alIsBuffer(buffer)) {
			alDeleteBuffers(1, &buffer);
		}
		return 0;
	}

	return buffer;
}

enum WaveType {
	WT_Sine,
	WT_Square,
	WT_Sawtooth,
	WT_Triangle,
	WT_Impulse,
};

static void ApplySin(ALfloat *data, ALdouble g, ALuint srate, ALuint freq) {
	ALdouble smps_per_cycle = (ALdouble)srate / freq;
	ALuint i;
	for(i = 0;i < srate;i++) {
		data[i] += (ALfloat)(sin(i/smps_per_cycle * 2.0*M_PI) * g);
	}
}

/* Generates waveforms using additive synthesis. Each waveform is constructed
 * by summing one or more sine waves, up to (and excluding) nyquist.
 */
 // TODO: expose creating synths
static ALuint CreateWave(enum WaveType type, ALuint freq, ALuint srate) {
	ALint data_size;
	ALfloat *data;
	ALuint buffer;
	ALenum err;
	ALuint i;

	data_size = srate * sizeof(ALfloat);
	data = (ALfloat *)calloc(1, data_size);
	if(type == WT_Sine)
		ApplySin(data, 1.0, srate, freq);
	else if(type == WT_Square)
		for(i = 1;freq*i < srate/2;i+=2)
			ApplySin(data, 4.0/M_PI * 1.0/i, srate, freq*i);
	else if(type == WT_Sawtooth)
		for(i = 1;freq*i < srate/2;i++)
			ApplySin(data, 2.0/M_PI * ((i&1)*2 - 1.0) / i, srate, freq*i);
	else if(type == WT_Triangle)
		for(i = 1;freq*i < srate/2;i+=2)
			ApplySin(data, 8.0/(M_PI*M_PI) * (1.0 - (i&2)) / (i*i), srate, freq*i);
	else if(type == WT_Impulse) {
		/* NOTE: Impulse isn't really a waveform, but it can still be useful to
		 * test (other than resampling, the ALSOFT_DEFAULT_REVERB environment
		 * variable can prove useful here to test the reverb response).
		 */
		for(i = 0;i < srate;i++)
			data[i] = (i%(srate/freq)) ? 0.0f : 1.0f;
	}

	/* Buffer the audio data into a new buffer object. */
	buffer = 0;
	alGenBuffers(1, &buffer);
	alBufferData(buffer, AL_FORMAT_MONO_FLOAT32, data, data_size, srate);
	free(data);

	/* Check if an error occured, and clean up if so. */
	err = alGetError();
	if(err != AL_NO_ERROR) {
		fprintf(stderr, "OpenAL Error: %s\n", alGetString(err));
		if(alIsBuffer(buffer)) {
			alDeleteBuffers(1, &buffer);
		}
		return 0;
	}

	return buffer;
}

OpenAL* OpenAL::instance = NULL;

OpenAL::OpenAL() {
}

OpenAL* OpenAL::getInstance(void) {
	if (!instance) {
		instance = new OpenAL();
	}
	return instance;
}

void OpenAL::setDistanceModel(const char* model) {
	switch (hash_string(model)) {
		case HASH_S16("inverse"):
			alDistanceModel(AL_INVERSE_DISTANCE);
			break;
		case HASH_S16("inverse clamped"):
			alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
			break;
		case HASH_S16("linear"):
			alDistanceModel(AL_LINEAR_DISTANCE);
			break;
		case HASH_S16("linear clamped"):
			alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
			break;
		case HASH_S16("exponent"):
			alDistanceModel(AL_EXPONENT_DISTANCE);
			break;
		case HASH_S16("exponent clamped"):
			alDistanceModel(AL_EXPONENT_DISTANCE_CLAMPED);
			break;
		case HASH_S16("none"):
			alDistanceModel(AL_NONE);
			break;
	}
}

float OpenAL::getDopplerFactor(void) {
	return alGetFloat(AL_DOPPLER_FACTOR);
}

void OpenAL::setDopplerFactor(float dopplerFactor) {
	alDopplerFactor(dopplerFactor);
}

float OpenAL::getSpeedOfSound(void) {
	return alGetFloat(AL_SPEED_OF_SOUND);
}

void OpenAL::setSpeedOfSound(float speed) {
	alSpeedOfSound(speed);
}

void OpenAL::setListenerPosition(float x, float y, float z) {
	alListener3f(AL_POSITION, x, y, z);
}

void OpenAL::setListenerVelocity(float x, float y, float z) {
	alListener3f(AL_VELOCITY, x, y, z);
}

void OpenAL::setListenerOrientation(float atx, float aty, float atz, float upx, float upy, float upz) {
	ALfloat orientation[] = {atx, aty, atz, upx, upy, upz};
	alListenerfv(AL_ORIENTATION, orientation);
}

ALuint OpenAL::newSource(dmBuffer::HBuffer* sourceBuffer) {
	/* Load the sound into a buffer. */
	ALuint buffer = loadWavFile(sourceBuffer);
	if (buffer == 0) {
		return 0;
	}

	/* Create the source to play the sound with. */
	ALuint source = 0;
	alGenSources(1, &source);
	alSourcei(source, AL_BUFFER, buffer);
	buffers[source] = buffer;
	if (alGetError() == AL_NO_ERROR) {
		alSource3f(source, AL_POSITION, 0., 0., 0.);
		alSource3f(source, AL_VELOCITY, 0., 0., 0.);
		alSourcef(source, AL_REFERENCE_DISTANCE, 50.);
	}
	return source;
}

void OpenAL::getSourceDefaults(ALuint source, float* gain, float* max_distance, float* rolloff_factor, float* reference_distance, float* min_gain, float* max_gain, float* cone_outer_gain, float* cone_inner_angle, float* cone_outer_angle, float* dx, float* dy, float* dz) {
	alGetSourcef(source, AL_GAIN, gain);
	alGetSourcef(source, AL_MAX_DISTANCE, max_distance);
	alGetSourcef(source, AL_ROLLOFF_FACTOR, rolloff_factor);
	alGetSourcef(source, AL_REFERENCE_DISTANCE, reference_distance);
	alGetSourcef(source, AL_MIN_GAIN, min_gain);
	alGetSourcef(source, AL_MAX_GAIN, max_gain);
	alGetSourcef(source, AL_CONE_OUTER_GAIN, cone_outer_gain);
	alGetSourcef(source, AL_CONE_INNER_ANGLE, cone_inner_angle);
	alGetSourcef(source, AL_CONE_OUTER_ANGLE, cone_outer_angle);
	alGetSource3f(source, AL_DIRECTION, dx, dy, dz);
}

const char* OpenAL::getSourceState(ALuint source) {
	ALenum state = 0;
	alGetSourcei(source, AL_SOURCE_STATE, &state);
	switch (state) {
		case AL_INITIAL:
			return "initial";
		case AL_PLAYING:
			return "playing";
		case AL_PAUSED:
			return "paused";
		case AL_STOPPED:
			return "stopped";
	}
	return "";
}

void OpenAL::setSourcePosition(ALuint source, float x, float y, float z) {
	alSource3f(source, AL_POSITION, x, y, z);
}

void OpenAL::setSourceVelocity(ALuint source, float x, float y, float z) {
	alSource3f(source, AL_VELOCITY, x, y, z);
}

void OpenAL::setSourceDirection(ALuint source, float x, float y, float z) {
	alSource3f(source, AL_DIRECTION, x, y, z);
}

void OpenAL::setSourcePitch(ALuint source, float pitch) {
	alSourcef(source, AL_PITCH, pitch);
}

void OpenAL::setSourceGain(ALuint source, float gain) {
	alSourcef(source, AL_GAIN, gain);
}

void OpenAL::setSourceMaxDistance(ALuint source, float max_distance) {
	alSourcef(source, AL_MAX_DISTANCE, max_distance);
}

void OpenAL::setSourceRolloffFactor(ALuint source, float rolloff_factor) {
	alSourcef(source, AL_ROLLOFF_FACTOR, rolloff_factor);
}

void OpenAL::setSourceReferenceDistance(ALuint source, float reference_distance) {
	alSourcef(source, AL_REFERENCE_DISTANCE, reference_distance);
}

void OpenAL::setSourceMinGain(ALuint source, float min_gain) {
	alSourcef(source, AL_MIN_GAIN, min_gain);
}

void OpenAL::setSourceMaxGain(ALuint source, float max_gain) {
	alSourcef(source, AL_MAX_GAIN, max_gain);
}

void OpenAL::setSourceConeOuterGain(ALuint source, float cone_outer_gain) {
	alSourcef(source, AL_CONE_OUTER_GAIN, cone_outer_gain);
}

void OpenAL::setSourceConeInnerAngle(ALuint source, float cone_inner_angle) {
	alSourcef(source, AL_CONE_INNER_ANGLE, cone_inner_angle);
}

void OpenAL::setSourceConeOuterAngle(ALuint source, float cone_outer_angle) {
	alSourcef(source, AL_CONE_OUTER_ANGLE, cone_outer_angle);
}

void OpenAL::setSourceLooping(ALuint source, bool looping) {
	alSourcei(source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}

void OpenAL::setSourceRelative(ALuint source, bool relative) {
	alSourcei(source, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
}

float OpenAL::getSourceTime(ALuint source) {
	float offset;
	alGetSourcef(source, AL_SEC_OFFSET, &offset);
	return offset;
}

void OpenAL::setSourceTime(ALuint source, float seconds) {
	alSourcef(source, AL_SEC_OFFSET, seconds);
}

void OpenAL::playSource(ALuint source) {
	alSourcePlay(source);
}

void OpenAL::pauseSource(ALuint source) {
	alSourcePause(source);
}

void OpenAL::rewindSource(ALuint source) {
	alSourceRewind(source);
}

void OpenAL::stopSource(ALuint source) {
	alSourceStop(source);
}

void OpenAL::removeSource(ALuint source) {
	ALuint buffer = buffers[source];
	alDeleteSources(1, &source);
	alDeleteBuffers(1, &buffer);
	buffers.erase(source);
}

void OpenAL::close(void) {
	for (std::map<ALuint, ALuint>::iterator i = buffers.begin(); i != buffers.end(); ++i) {
		alDeleteSources(1, &i->first);
		alDeleteBuffers(1, &i->second);
	}
	buffers.clear();
}