/* Copyright (C) 2011 Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "QtAudioDriver.h"

#include <QtMultimediaKit/QAudioOutput>

#include <mt32emu/mt32emu.h>

#include "../ClockSync.h"
#include "../Master.h"
#include "../MasterClock.h"
#include "../QSynth.h"

using namespace MT32Emu;

class WaveGenerator : public QIODevice {
private:
	ClockSync clockSync;
	QSynth *synth;
	QAudioOutput *audioOutput;
	double sampleRate;
	qint64 latency;

public:
	WaveGenerator(QSynth *useSynth, QAudioOutput *useAudioOutput, double sampleRate) : synth(useSynth), audioOutput(useAudioOutput), sampleRate(sampleRate), latency(0) {
		open(QIODevice::ReadOnly);
	}

	void setLatency(qint64 newLatency) {
		latency = newLatency;
	}

	qint64 readData(char *data, qint64 len) {
		qint64 firstSampleAudioNanos = audioOutput->processedUSecs() * 1000;
		qint64 firstSampleMasterClockNanos = clockSync.sync(firstSampleAudioNanos) - latency * 20;

		return synth->render((Bit16s *)data, (unsigned int)(len >> 2), firstSampleMasterClockNanos, sampleRate) << 2;
	}

	qint64 writeData(const char *data, qint64 len) {
		Q_UNUSED(data);
		Q_UNUSED(len);
		return 0;
	}
};

QtAudioStream::QtAudioStream(QSynth *useSynth, unsigned int useSampleRate) : sampleRate(useSampleRate) {
	QAudioFormat format;
	format.setFrequency(sampleRate);
	format.setChannels(2);
	format.setSampleSize(16);
	format.setCodec("audio/pcm");
	// libmt32emu produces samples in native byte order
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
	format.setByteOrder(QAudioFormat::LittleEndian);
#else
	format.setByteOrder(QAudioFormat::BigEndian);
#endif
	format.setSampleType(QAudioFormat::SignedInt);

	audioOutput = new QAudioOutput(format);
	waveGenerator = new WaveGenerator(useSynth, audioOutput, sampleRate);
}

QtAudioStream::~QtAudioStream() {
	delete audioOutput;
	delete waveGenerator;
}

bool QtAudioStream::start() {
	audioOutput->start(waveGenerator);
	waveGenerator->setLatency(2 * MasterClock::NANOS_PER_SECOND * audioOutput->bufferSize() / (4 * sampleRate));
	return true;
}

void QtAudioStream::close() {
	audioOutput->stop();
}

QtAudioDefaultDevice::QtAudioDefaultDevice(const QtAudioDriver *driver) : AudioDevice(driver, "default", "Default") {
}

QtAudioStream *QtAudioDefaultDevice::startAudioStream(QSynth *synth, unsigned int sampleRate) const {
	QtAudioStream *stream = new QtAudioStream(synth, sampleRate);
	if (stream->start()) {
		return stream;
	}
	delete stream;
	return NULL;
}

QtAudioDriver::QtAudioDriver(Master *useMaster) : AudioDriver("qtaudio", "QtAudio") {
	Q_UNUSED(useMaster);
	loadAudioSettings();
}

QtAudioDriver::~QtAudioDriver() {
}

QList<AudioDevice *> QtAudioDriver::getDeviceList() const {
	QList<AudioDevice *> deviceList;
	deviceList.append(new QtAudioDefaultDevice(this));
	return deviceList;
}