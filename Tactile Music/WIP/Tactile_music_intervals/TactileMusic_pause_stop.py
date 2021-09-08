#TactileMain.py is the code from a communicating computer.  Is the code meant to work with main.py to provide signal processing.
#REQUIREMENTS:
#pip install bleak
#pip install scipy
#pip install simpleaudio
#pip install librosa
#pip install keyboard

from scipy.io import wavfile
from scipy.fft import fft
from scipy import signal
import math
import numpy
import simpleaudio
import time
from bleak import BleakClient
import bleak
import asyncio
import librosa
import keyboard

TARGET_UUID = 'b7328f9c-c89e-4d74-9a5e-000000000000'
UART_TX = 'b7328f9c-c89e-4d74-9a5e-000000000001' #UART'S TX is Bleak's RX
UART_RX = 'b7328f9c-c89e-4d74-9a5e-000000000002' #UART'S RX is Bleak's TX

async def play_file(filename):
    TARGET_UUID = 'b7328f9c-c89e-4d74-9a5e-000000000000'
    UART_TX = 'b7328f9c-c89e-4d74-9a5e-000000000001' #UART'S TX is Bleak's RX
    UART_RX = 'b7328f9c-c89e-4d74-9a5e-000000000002' #UART'S RX is Bleak's TX

    address = "24:0A:C4:60:97:22"  #NOTE: MAC address is per device, so this needs to be changed
    while True:
        try:
            client = BleakClient(address)
            await client.connect()
            print("Connected!")

            read_string = b''
            read_string = await client.read_gatt_char(UART_TX)
            print("TEST STRING: ", read_string.decode('UTF-8'))
            break

        except Exception as e:
            print(e)
            print('Trying to reconnect...')
            continue

    print("Processing audio...")
    #Read in data
    data, samplerate = librosa.load(filename, sr=8000)
    #If 2 channel audio, take a channel and process it as mono
    if(len(data.shape) >= 2 and data.shape[1] == 2):
        temp_data = []
        for i in range(0, len(data)):
            temp_data.append(data[i][0])
        data = numpy.array(temp_data)

    rawdata = data #Saving rawdata for playback

    bytesPerSample = 2


    if(type(data[0]) == numpy.float32):
        #Float32 is 4 bytes in the desired range of -1 to 1
        bytesPerSample = 4
    elif(type(data[0]) == numpy.int16):
        #int16 is 2 bytes -32768 to 32767
        data = numpy.array(data)
        data = data.astype(numpy.float32)
        data = data / 32768
    elif(type(data[0]) == numpy.int32):
        #int32 is 4 bytes -2147483648 to 2147483647
        data = numpy.array(data)
        data = data.astype(numpy.float32)
        data = data / 2147483648
        bytesPerSample = 4
    elif(type(data[0]) == numpy.uint8):
        #uint is 1 byte 0 to 255
        data = numpy.array(data)
        data = data.astype(numpy.float32)
        data = (data / 128) - 1
        bytesPerSample = 1

    interval = 0.5 #Interval is .05 sec to start to leave computation time
    samplesPerInterval = math.ceil(samplerate * interval) #NOTE: This rounds up, so in instances where samplerate * interval isn't an integer, there may be desync issues, although with conventionally large sampling rates and a clean interval like .1 (aka divide by 10) that shouldn't be a problem.
    numSegments = math.ceil(len(data) / samplesPerInterval)

    #Sets up intensities and prev_intensities
    intensities = {}
    prev_intensities = {}
    min_cycles = {}
    DATA = 0
    LOW_PASS = 1
    BAND_PASS = 2
    HIGH_PASS = 3
    NUM_FILTERS = 4
    MIN_CYCLES = 2

    for i in range(0, NUM_FILTERS):
        intensities[i] = 0.0
        prev_intensities[i] = 0.0
        min_cycles[i] = 0

    #Sets up pause
    isPaused = False
    prevSpace = False
    startPlay = True
    # ------------ Power computation ------------
    #NOTE: Since samplesPerInterval is dependent on samplerate, with higher sample rate audio, there is a significant performance hit.  Audio should be downsampled, either in code (NYI) or in Audacity, before processing
    for segment in range(0,numSegments):

        #Set pause boolean on spacebar press.  Spinlock until space is pressed again
        if keyboard.is_pressed('space'):
            if(prevSpace == False):
                isPaused = True
                #NOTE: Seg faults when stopping too fast, probably because of stop not being as fast as the intervals?  Not entirely sure, but it's something worth noting :).
                play_obj.stop()
            prevSpace = True
        else:
            prevSpace = False

        while(isPaused):
            if(keyboard.is_pressed('space')):
                if(prevSpace == False):
                    startPlay = True
                    isPaused = False
                prevSpace = True
            else:
                prevSpace = False

        starttime = time.time()

        #Calculate the ffts of specifically the desired slice of time using some simple indexing
        data_fft = fft(data[(segment * samplesPerInterval):((segment+1) * samplesPerInterval)], samplerate)

        #Multiply by the conjugate element by element to get the power, removing all imaginary components.
        #NOTE: Loops are all separate to allow filters to be completely independent, with an arbitrary amount of filter coefficients
        for i in range(0, len(data_fft)):
            data_fft[i] = data_fft[i] * numpy.conj(data_fft[i])

        #Dividing by the number of samples per interval * samplerate so we can be normalized across sampling rates assuming the same interval
        #PRECONDITION: 8000 / samplerate = 1.  Otherwise multiply data_fft by 8000/samplerate
        data_fft = data_fft / (samplesPerInterval)

        #TEMP: Manually setting min and max as separate variables
        #PRECONDITION: 8Khz sampling rate
        lowpass_min = 0
        lowpass_max = 1000
        bandpass_min = 1000
        bandpass_max = 2000
        highpass_min = 2000
        highpass_max = 4000

        #Calculate the intensities of the ffts.

        for i in range(0, NUM_FILTERS):
            intensities[i] = 0.0

        for i in range(0, len(data_fft)):
            intensities[DATA] += data_fft[i]

        for i in range(lowpass_min, lowpass_max):
            intensities[LOW_PASS] += data_fft[i]

        for i in range(bandpass_min, bandpass_max):
            intensities[BAND_PASS] += data_fft[i]

        for i in range(highpass_min, highpass_max):
            intensities[HIGH_PASS] += data_fft[i]

        write_bytes = b''
        #NOTE: ESP32 is currently hard coded to expect 4 filters; adding more filters without changing the code will probably break it
        for i in range(0, NUM_FILTERS):
            #TODO: Add some code to have a dynamic min threshold to trigger the intensities based on the average reading/2
            if(intensities[i] - prev_intensities[i] > (prev_intensities[i] + prev_intensities[DATA]/4)/4 * 1.5):
                modulated_intensity = 1023
                min_cycles[i] = MIN_CYCLES
            elif(intensities[i] - prev_intensities[i] > (prev_intensities[i] + prev_intensities[DATA]/4)/4 * 1.25 or min_cycles[i] > 0):
                modulated_intensity = 512
                min_cycles[i] -= 1
            else:
                modulated_intensity = 0

            # if (intensities[i] >= 800):
            #     modulated_intensity = 1023
            # elif (800 > intensities[i] >= 600):
            #     modulated_intensity = 800
            # elif (600 > intensities[i] >= 400):
            #     modulated_intensity = 512
            # elif (400 > intensities[i] >= 200):
            #     modulated_intensity = 200
            # else:
            #     modulated_intensity = 0

            write_bytes += modulated_intensity.to_bytes(2, 'big')
            prev_intensities[i] = intensities[i]
        await client.write_gatt_char(UART_RX, write_bytes)

        #Playing audio after computation but before wait to attempt to match up as best as possible.  Not sure if this is better or worse than playing at beginning of loop
        if(startPlay):
            play_obj = simpleaudio.play_buffer(rawdata[segment * samplesPerInterval:], 1, bytesPerSample, samplerate)
            startPlay = False

        #Calculate time to sleep, but ensure sleeptime isn't negative to not cause an error with time.sleep
        desired_sleep_time = interval - time.time() + starttime
        sleep_time = max(desired_sleep_time, 0)
        time.sleep(sleep_time)
        if desired_sleep_time < 0:
            print("----- processing time exceeded time interval -------")
            print("segment: ", segment, "\n", "sleep needed:", desired_sleep_time,"\n")
        # print("Segment: %d\t\tTime Slept: %f\t\tSleep Needed: %f\t\tData Intensity: %f\n" % (segment, sleep_time, desired_sleep_time, intensities[DATA]), flush=True) #Print at the end when we'd hypothetically do power calculations
        # print("\t\t\tLow Pass: %f\t\tBand Pass: %f\t\tHigh Pass: %f\n\n" % (intensities[LOW_PASS], intensities[BAND_PASS], intensities[HIGH_PASS]), flush=True)

    #Wait for the signal to finish playing
    play_obj.wait_done()
    input("waiting to stop all motors: press enter")
    stop_all = 0
    message = stop_all.to_bytes(8, 'big')
    await client.write_gatt_char(UART_RX, message)



# asyncio.run(play_file("test.wav"))
asyncio.run(play_file("Spoopy.wav"))
