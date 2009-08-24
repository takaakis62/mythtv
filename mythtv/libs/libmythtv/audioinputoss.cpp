/*
 * Copyright (C) 2007  Anand K. Mistry
 * Copyright (C) 2007  Daniel Kristjansson
 * Copyright (C) 2003-2007 Others who contributed to NuppelVideoRecorder.cpp
 * Copyright (C) 2008  Alan Calvert
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "mythconfig.h"
#if HAVE_SYS_SOUNDCARD_H
    #include <sys/soundcard.h>
#elif HAVE_SOUNDCARD_H
    #include <soundcard.h>
#endif

#include "audioinputoss.h"
#include "mythcontext.h"
#include <fcntl.h>
#include <sys/ioctl.h>

#define LOC     QString("AudioInOSS: ")
#define LOC_DEV QString("AudioInOSS(%1): ").arg(m_device_name.constData())
#define LOC_ERR QString("AudioInOSS(%1) Error: ").arg(m_device_name.constData())

AudioInputOSS::AudioInputOSS(const QString &device) : AudioInput(device)
{
    if (!device.isEmpty())
        m_device_name = QByteArray(device.toAscii());
    else
        m_device_name = QByteArray();
    dsp_fd = -1;
}

bool AudioInputOSS::Open(uint sample_bits, uint sample_rate, uint channels)
{
    m_audio_sample_bits = sample_bits;
    m_audio_sample_rate = sample_rate;
    //m_audio_channels = channels;
    int chk;

    if (IsOpen())
        Close();

    // Open the device
    dsp_fd = open(m_device_name.constData(), O_RDONLY);
    if (dsp_fd < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "open failed " + strerror(dsp_fd));
        Close();
        return false;
    }

    chk = 0; // disable input for now
    ioctl(dsp_fd, SNDCTL_DSP_SETTRIGGER, &chk);

    // Set format
    int format, choice;
    QString tag = QString::null;
    switch (sample_bits)
    {
        case 8:
            choice = AFMT_U8;
            tag = "AFMT_U8";
            break;
        case 16:
        default:
#if HAVE_BIGENDIAN
            choice = AFMT_S16_BE;
            tag = "AFMT_S16_BE";
#else
            choice = AFMT_S16_LE;
            tag = "AFMT_S16_LE";
#endif
            break;
    }
    format = choice;
    if ((chk = ioctl(dsp_fd, SNDCTL_DSP_SETFMT, &format) < 0))
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR  +
                QString("failed to set audio format %1").arg(tag) +
                strerror(chk));
        Close();
        return false;
    }
    if (format != choice)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("set audio format not %1 as requested").arg(tag));
        Close();
        return false;
    }

    // sample size
    m_audio_sample_bits = choice = sample_bits;
    if ((chk = ioctl(dsp_fd, SNDCTL_DSP_SAMPLESIZE, &m_audio_sample_bits)) < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("failed to set audio sample bits to %1 ")
                .arg(sample_bits) + strerror(chk));
        Close();
        return false;
    }
    if (m_audio_sample_bits != choice)
        VERBOSE(VB_IMPORTANT, LOC_ERR + QString("requested %1 sample bits, got %2")
                            .arg(choice).arg(m_audio_sample_bits));
    // channels
    m_audio_channels = choice = channels;
    if ((chk = ioctl(dsp_fd, SNDCTL_DSP_CHANNELS, &m_audio_channels)) < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + QString("failed to set audio channels to %1 ")
                                     .arg(channels) + strerror(chk));
        Close();
        return false;
    }
    if (m_audio_channels != choice)
        VERBOSE(VB_IMPORTANT, LOC_ERR + QString("requested %1 channels, got %2")
                .arg(choice).arg(m_audio_channels));

    // sample rate
    int choice_sample_rate;
    m_audio_sample_rate = choice_sample_rate = sample_rate;
    if ((chk = ioctl(dsp_fd, SNDCTL_DSP_SPEED, &m_audio_sample_rate)) < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("failed to set sample rate to %1 ")
                .arg(sample_rate) + strerror(chk));
        Close();
        return false;
    }
    if (m_audio_sample_rate != choice_sample_rate)
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("requested sample rate %1, got %2")
                .arg(choice_sample_rate).arg(m_audio_sample_rate));
    VERBOSE(VB_AUDIO, LOC_DEV + "device open");
    return true;
}

void AudioInputOSS::Close(void)
{
    if (IsOpen())
        close(dsp_fd);
    dsp_fd = -1;
    m_audio_sample_bits = 0;
    m_audio_sample_rate = 0;
    m_audio_channels = 0;
    VERBOSE(VB_AUDIO, LOC_DEV + "device closed");
}

bool AudioInputOSS::Start(void)
{
    bool started = false;
    if (IsOpen())
    {
        int chk;
        int trig = 0; // clear
        ioctl(dsp_fd, SNDCTL_DSP_SETTRIGGER, &trig);
        trig = PCM_ENABLE_INPUT; // enable input
        if ((chk = ioctl(dsp_fd, SNDCTL_DSP_SETTRIGGER, &trig)) < 0)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "Start() failed " + strerror(chk));
        }
        else
        {
            VERBOSE(VB_AUDIO, LOC_DEV + "capture started");
            started = true;
        }
    }
    return started;
}

bool AudioInputOSS::Stop(void)
{
    bool stopped = false;
    int chk;
    int trig = 0;
    if ((chk = ioctl(dsp_fd, SNDCTL_DSP_SETTRIGGER, &trig)) < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "stop action failed " + strerror(chk));
    }
    else
    {
        stopped = true;
        VERBOSE(VB_AUDIO, LOC_DEV + "capture stopped");
    }
    return stopped;
}

int AudioInputOSS::GetBlockSize(void)
{
    int frag = 0;
    if (IsOpen())
    {
        int chk;
        if ((chk = ioctl(dsp_fd, SNDCTL_DSP_GETBLKSIZE, &frag)) < 0)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    QString("fragment size query failed, returned %1 ")
                    .arg(frag) + strerror(chk));
            frag = 0;
        }
    }
    VERBOSE(VB_AUDIO, LOC_DEV + QString("block size %1").arg(frag));
    return frag;
}

int AudioInputOSS::GetSamples(void *buffer, uint num_bytes)
{
    uint bytes_read = 0;
    if (IsOpen())
    {
        unsigned char* bufptr = (unsigned char*)buffer;
        int this_read;
        int retries = 0;
        while (bytes_read < num_bytes && retries < 3)
        {
            this_read = read(dsp_fd, buffer, num_bytes - bytes_read);
            if (this_read < 0)
            {
                VERBOSE(VB_IMPORTANT, LOC_ERR + "GetSamples read failed " +
                        strerror(this_read));
            }
            else
            {
                bytes_read += this_read;
                bufptr += this_read;
            }
            ++retries;
        }
        if (num_bytes > (uint)bytes_read)
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    QString("GetSamples short read, %1 of %2 bytes")
                    .arg(bytes_read).arg(num_bytes));
    }
    return bytes_read;
}

int AudioInputOSS::GetNumReadyBytes(void)
{
    int readies = 0;
    if (IsOpen())
    {
        audio_buf_info ispace;
        int chk;
        if ((chk = ioctl(dsp_fd, SNDCTL_DSP_GETISPACE, &ispace)) < 0)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    QString("get ready bytes failed, returned %1 ")
                    .arg(ispace.bytes) + strerror(chk));
        }
        else if ((readies = ispace.bytes) > 0)
            VERBOSE(VB_AUDIO|VB_EXTRA, LOC_DEV + QString("ready bytes %1")
                    .arg(readies));
    }
    return readies;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */