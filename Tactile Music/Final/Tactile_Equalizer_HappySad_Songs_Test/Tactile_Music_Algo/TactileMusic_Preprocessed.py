#TactileMain.py is the code from a communicating computer.  Is the code meant to work with main.py to provide signal processing.
#REQUIREMENTS:
#pip install bleak
#pip install scipy #will install both scipy and numpy
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
import statistics

# global variable, if these are such, do not seem to be recognized when using asynch
# TARGET_UUID = 'b7328f9c-c89e-4d74-9a5e-000000000000'
# UART_TX = 'b7328f9c-c89e-4d74-9a5e-000000000001' #UART'S TX is Bleak's RX
# UART_RX = 'b7328f9c-c89e-4d74-9a5e-000000000002' #UART'S RX is Bleak's TX

async def convert_to_float32(data):
    bytesPerSample = 2
    # print("Data[0] type is: ", type(data[0]) )
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
    # print("Data after processing is:", type(data[0]))
    # print("Hence, bytesPerSample is:", bytesPerSample)
    # print("Also, data length is:", len(data))

    return (bytesPerSample, data)

async def audio_to_tactile(data, samplerate, interval):
    # print("Pre - Processing audio...")
    intensities = {}
    prev_intensities = {}
    min_cycles = {}
    vibrations = []
    DATA = 0
    LOW_PASS = 1
    BAND_PASS = 2
    HIGH_PASS = 3
    NUM_FILTERS = 4
    MIN_CYCLES = 2
    intensity_dict = {'Original': [], "Low Pass": [], "Band Pass": [], "High Pass": []}
    #Sets up intensities and prev_intensities
    for i in range(0, NUM_FILTERS):
        intensities[i] = 0.0
        prev_intensities[i] = 0.0
        min_cycles[i] = 0

    samplesPerInterval = math.ceil(samplerate * interval)
    numSegments = math.ceil(len(data) / samplesPerInterval)

    #TEMP: Manually setting min and max as separate variables
    #PRECONDITION: 8Khz sampling rate
    lowpass_min = 0
    lowpass_max = 1000
    bandpass_min = 1000
    bandpass_max = 2000
    highpass_min = 2000
    highpass_max = 4000

    #Using "Hotel California" as the baseline for threshold values
    #current values are based on the average for the entire song, independent of any interval
    Baseline_avg = 12.86        #61.2 for each interval
    Baseline_LPF_avg = 48.51    #24.2
    Baseline_BPF_avg = 2.06     #3.8
    Baseline_HPF_avg = 0.43     #2.6

    #Adjust the thresholds according to the mean of the song w.r.t. baseline
    song_fft = fft(data[:], samplerate)
    song_real = song_fft *numpy.conj(song_fft)
    # print("FFT of the entire song \n", song_fft)
    # print("The real part of it is \n", song_real.real)
    # print("length of the FFT is:", len(song_fft), len(song_real))
    # input()
    Original_avg = statistics.mean(song_real.real)
    LPF_avg = statistics.mean( (song_real[lowpass_min:lowpass_max]).real)
    BPF_avg = statistics.mean( (song_real[bandpass_min:bandpass_max]).real)
    HPF_avg = statistics.mean( (song_real[highpass_min:highpass_max]).real)
    print("The averages of the FFT values are: ", Original_avg, LPF_avg, BPF_avg, HPF_avg)

    Original_factor = Original_avg/Baseline_avg
    LPF_factor = LPF_avg/ Baseline_LPF_avg
    BPF_factor = BPF_avg/ Baseline_BPF_avg
    HPF_factor = HPF_avg/ Baseline_HPF_avg
    print("Threshold factors are: \n", Original_factor, LPF_factor, BPF_factor, HPF_factor)
    # input()

    # ------------ Power computation ------------
    #NOTE: Since samplesPerInterval is dependent on samplerate, with higher sample rate audio, there is a significant performance hit.  Audio should be downsampled, either in code (NYI) or in Audacity, before processing

    for segment in range(0,numSegments):
    #Calculate the ffts of specifically the desired slice of time using some simple indexing
        data_fft = fft(data[(segment * samplesPerInterval):((segment+1) * samplesPerInterval)], samplerate)
        #Multiply by the conjugate element by element to get the power, removing all imaginary components.
        #NOTE: Loops are all separate to allow filters to be completely independent, with an arbitrary amount of filter coefficients
        for i in range(0, len(data_fft)):
            data_fft[i] = data_fft[i] * numpy.conj(data_fft[i])

        #Dividing by the number of samples per interval * samplerate so we can be normalized across sampling rates assuming the same interval
        #PRECONDITION: 8000 / samplerate = 1.  Otherwise multiply data_fft by 8000/samplerate
        data_fft = data_fft / (samplesPerInterval)

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
            # #TODO: Add some code to have a dynamic min threshold to trigger the intensities based on the average reading/2
            # if(intensities[i] - prev_intensities[i] > (prev_intensities[i] + prev_intensities[DATA]/4)/4 * 1.5):
            #     modulated_intensity = 1023
            #     # min_cycles[i] = MIN_CYCLES
            # elif(intensities[i] - prev_intensities[i] > (prev_intensities[i] + prev_intensities[DATA]/4)/4 * 1.25 or min_cycles[i] > 0):
            #     modulated_intensity = 512
            #     # min_cycles[i] -= 1
            # else:
            #     modulated_intensity = 0

            #Using "Hotel California" as the baseline for threshold values
            #Thresholds established based on 0.2 seconds intervals and emperically playing on LEDs
            if i == LOW_PASS:
                threshold = [5, 25, 50]
                tuned_threshold = [element * LPF_factor for element in threshold]
            elif i == BAND_PASS:
                threshold = [1, 3, 6]
                tuned_threshold = [element * BPF_factor for element in threshold]
            elif i == HIGH_PASS:
                threshold = [0.5, 1.5, 5]
                tuned_threshold = [element * HPF_factor for element in threshold]
            else:
                threshold = [20, 60, 90] #for the original unfiltered data OR random stuff
                tuned_threshold = [element * Original_factor for element in threshold]

            #only the first value in the threshold list is being used
            if (intensities[i] >= (tuned_threshold[1])/2):
                modulated_intensity = 850
            # elif (threshold[2] > intensities[i] >= tuned_threshold[1]):
            #     modulated_intensity = 400 #512
            elif ((threshold[1])/2 > intensities[i] >= tuned_threshold[0]):
                modulated_intensity = 700 #100
            # elif ((tuned_threshold[0])/2 <= intensities[i] < tuned_threshold[0]):
            #     modulated_intensity = 500 #100
            elif (tuned_threshold[0] > intensities[i] >= (tuned_threshold[0])/2):
                modulated_intensity = 500 #100
            elif ((tuned_threshold[0])/2 > intensities[i]):
                modulated_intensity = 0 #100
            # elif (intensities[i] < (tuned_threshold[0])/2):
            #     modulated_intensity = 0 #100

            # if (intensities[i] >= tuned_threshold[2]):
            #     modulated_intensity = 850
            # elif (threshold[2] > intensities[i] >= tuned_threshold[1]):
            #     modulated_intensity = 100#512
            # elif (threshold[1] > intensities[i] >= tuned_threshold[0]):
            #     modulated_intensity = 0#100
            else:
                modulated_intensity = 0

            write_bytes += modulated_intensity.to_bytes(2, 'big')
            # prev_intensities[i] = intensities[i]
        vibrations.append(write_bytes) #8 byte data for each segment is appended into vibration
        #vibration should have as many elements as the num of segments.

        ###### computing & printing the values to figure out possible thresholds  #########
        # it gives similar value regardless of the interval being 0.2s, 0.1s, 0.05s.
    #     intensity_dict["Original"].append(round( float(intensities[DATA]),2 ))
    #     intensity_dict["Low Pass"].append(round(float(intensities[LOW_PASS]),2))
    #     intensity_dict["Band Pass"].append(round(float(intensities[BAND_PASS]),2))
    #     intensity_dict["High Pass"].append(round(float(intensities[HIGH_PASS]),2))
    #
    # # print("Here is the cummulative of all: \n", intensity_dict)
    # print("Original info:---\n", "mean =" , statistics.mean(intensity_dict["Original"]), "\n median =", statistics.median(intensity_dict["Original"]), "\n mode =", statistics.mode(intensity_dict["Original"]))
    # print("Low Pass info:---\n", "mean =" , statistics.mean(intensity_dict["Low Pass"]), "\n median =", statistics.median(intensity_dict["Low Pass"]), "\n mode =", statistics.mode(intensity_dict["Low Pass"]))
    # print("Band Pass info:---\n", "mean =" , statistics.mean(intensity_dict["Band Pass"]), "\n median =", statistics.median(intensity_dict["Band Pass"]), "\n mode =", statistics.mode(intensity_dict["Band Pass"]))
    # print("High Pass info:---\n", "mean =" , statistics.mean(intensity_dict["High Pass"]), "\n median =", statistics.median(intensity_dict["High Pass"]), "\n mode =", statistics.mode(intensity_dict["High Pass"]))
    #
    # input("Waiting... you may want to kill it now.")
    ########-----------------------------------------------

    return vibrations


async def play_file(filename):
    TARGET_UUID = 'b7328f9c-c89e-4d74-9a5e-000000000000'
    UART_TX = 'b7328f9c-c89e-4d74-9a5e-000000000001' #UART'S TX is Bleak's RX
    UART_RX = 'b7328f9c-c89e-4d74-9a5e-000000000002' #UART'S RX is Bleak's TX

    address = "24:6f:28:7a:91:76" #"24:0A:C4:60:97:22"  #NOTE: MAC address is per device, so this needs to be changed
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
    data, samplerate = librosa.load(filename, sr=8000)#Read in data
    #If 2 channel audio, take a channel and process it as mono
    if(len(data.shape) >= 2 and data.shape[1] == 2):
        temp_data = []
        for i in range(0, len(data)):
            temp_data.append(data[i][0])
        data = numpy.array(temp_data)
    old_data = data #just reassigning to a variable for my sanity
    rawdata = data #Saving rawdata for playback

    bytesPerSample, data = await convert_to_float32(old_data)#Converting different audio types into same range

    #****************************#
    interval = 0.1 #Interval is .05 sec to start to leave computation time
    #****************************#
    motor_vibrations = await audio_to_tactile(data, samplerate, interval)#Processing the audio file to generate a list of vibrations (8 bytes ea) to be sent one segment at a time.
    print("Audio processing completed!")
    print()

    samplesPerInterval = math.ceil(samplerate * interval)
    numSegments = math.ceil(len(data) / samplesPerInterval)
    #NOTE: Rounds up, so in instances where samplerate * interval isn't an integer, there may be desync issues.

    #Sets up pause
    isPaused = False  #not paused as of now
    prevSpace = False #space was not pressed previously
    stop_all = 0
    stop_message = stop_all.to_bytes(8, 'big') #message to stop all motors

    for segment in range(0,numSegments):

        #Set pause boolean on spacebar press. Spinlock until space is pressed again.
        #NOTE: Seg faults when stopping too fast, probably because of stop not being as fast as the intervals?  Not entirely sure, but it's something worth noting :).

        if keyboard.is_pressed('space'): #is_pressed will always return True while space is pressed
            if prevSpace == False:       #when space is pressed the first time
                print("You chose to pause...")
                isPaused = True
                play_obj.stop()
                await client.write_gatt_char(UART_RX, stop_message) #stop motors when song is paused
            prevSpace = True
        else: #starts here, 'cos space is not pressed at the start.
            prevSpace = False

        while isPaused:
            # print("Inside while - Paused!")
            if keyboard.is_pressed('space'):
                if prevSpace == False:
                    isPaused = False
                    print("Pause released!")
                prevSpace = True
            else:#once paused, it comes here first - and waits here till space is pressed again.
                prevSpace = False

        starttime = time.time()

        play_obj = simpleaudio.play_buffer(rawdata[segment * samplesPerInterval:(segment * samplesPerInterval)+samplesPerInterval], 1, bytesPerSample, samplerate)

        '''
        # LEDs look good when they are ON for the entire segment.
        # But vibration changes are hard to detect or
        # motor feels like it is always ON, if on for the entire segment.
        # Vibrational changes felt easier to perceive when on for a short period.
        '''
        await client.write_gatt_char(UART_RX, motor_vibrations[segment])
        await asyncio.sleep(0.025)
        await client.write_gatt_char(UART_RX, stop_message)
        #Calculate time to sleep, but ensure sleeptime isn't negative to not cause an error with time.sleep
        desired_sleep_time = interval - time.time() + starttime
        sleep_time = max(desired_sleep_time, 0)
        await asyncio.sleep(sleep_time)
        # await asyncio.sleep(10) #Test to check for synchronicity
        # play_obj.wait_done()#forces the audio segment to be completed, but adds a perceptable delay after every segment
        # time.sleep(sleep_time)
        if desired_sleep_time < 0:
            print("----- processing time exceeded time interval -------")
            print("segment: ", segment, "\n", "sleep needed:", desired_sleep_time,"\n")

    #Wait for the signal to finish playing
    play_obj.wait_done()
    input("waiting to stop all motors: press enter")
    await client.write_gatt_char(UART_RX, stop_message)#stop all motors before exiting

#main
# asyncio.run(play_file("test.wav")) #current algo/thresholds dont work for this
# asyncio.run(play_file("Spoopy.wav"))
# asyncio.run(play_file("Bobby-McFerrin-Don-t-Worry-Be-Happy-CALM.wav"))
# asyncio.run(play_file("Eagles-Hotel-California.wav"))
# asyncio.run(play_file("Celine-Dion-My-Heart-will-go-on-Titanic.wav"))
#Happy
# asyncio.run(play_file("Macarena-Los-del-Rio-Hey-Macarena_HAPPY.wav"))
# asyncio.run(play_file("BrownSugar-by-RollingStones-Happy.wav"))
# asyncio.run(play_file("Walk-The-Moon-_-Shut-Up-and-Dance-With-Me_HAPPY.wav"))
#Sad
# asyncio.run(play_file("The-First-Time-Ever-I-Saw-Your-Face_SAD.wav"))
# asyncio.run(play_file("The-Lion-King-To-Die-For.wav"))
# asyncio.run(play_file("Say-Something-A-Great-Big-World_-Christina-Aguilera_SAD.wav"))

############################################################################################

#Happy
# asyncio.run(play_file("HAPPY_Mariya_Takeuchi_Plastic_Love.wav"))
# asyncio.run(play_file("HAPPY_The_Rolling_Stones_Start_Me_Up.wav"))
# asyncio.run(play_file("HAPPY_The_Romantics_What_I_Like_About_You.wav"))

# asyncio.run(play_file("HAPPY_Ambrosia_How_Much_I_Feel.wav"))
# asyncio.run(play_file("HAPPY_StevePerry_Foolish_Heart.wav"))

#Sad
# asyncio.run(play_file("SAD_Chris_Isaak_Blue_Spanish_Sky.wav"))
# asyncio.run(play_file("SAD_Joe_Cocker_You_Are_So_Beautiful"))

# asyncio.run(play_file("SAD_Billy_Joel_Piano_Man.wav"))
# asyncio.run(play_file("SAD_George_Michael_Spinning_The_Wheel.wav"))

#Calm
# asyncio.run(play_file("CALM_Doobie_Brothers_What_A_fool_Believes.wav"))
# asyncio.run(play_file("CALM_Kate_Bush_Cloudbusting.wav"))

# asyncio.run(play_file("CALM_Anri_Last_Summer_Whisper.wav"))
# asyncio.run(play_file("CALM_George_Michael_Move_On.wav"))

#Angry
# asyncio.run(play_file("ANGRY_Mötley_Crüe_Kickstart_my_Heart.wav"))
# asyncio.run(play_file("ANGRY_Guns_N'_Roses_Welcome_To_The_Jungle.wav"))

# asyncio.run(play_file("ANGRY_Jane's_Addiction_Jane_Says.wav"))
# asyncio.run(play_file("ANGRY_Queen_Under_Pressure.wav"))

############################################################################################

#####Happy

#Brass
# asyncio.run(play_file("fun-retro-upbeat-SBA-300515932.wav"))
#Guitar
# asyncio.run(play_file("audioblocks-morning-sun_SlsU0CqjH.wav"))
#Orchestra
# asyncio.run(play_file("upbeat-funky-good-times-SBA-300270097.wav"))
#Precussion
# asyncio.run(play_file("serious-funkin-business-SBA-346475726.wav"))
#Piano
# asyncio.run(play_file("audioblocks-maca_SN9iHvqLL.wav"))
#Synth
# asyncio.run(play_file("audioblocks-perfect-wave_rbVWCp0UK.wav"))
#Woodwind
# asyncio.run(play_file("little-duke-SBA-300505158.wav"))

#####Sad

#Brass
asyncio.run(play_file("western-adventure-version-6-1-min-lyric-theme-SBA-300505680.wav"))
#Guitar
# asyncio.run(play_file("sleeping-peacefully_GJkBQ8ru.wav"))
#Orchestra
# asyncio.run(play_file("audioblocks-last-dream_rq2WZb_qU.wav"))
#Precussion
# asyncio.run(play_file("blac-demarco-instrumental-version-SBA-300472273.wav"))
#Piano
# asyncio.run(play_file("worlds-apart-SBA-300481567.wav"))
#Synth
# asyncio.run(play_file("waking-life-SBA-300480598.wav"))
#Woodwind
# asyncio.run(play_file("sad-drama-cinematic-strings-trailer-SBA-300515876.wav"))

#####Calm

#Brass
# asyncio.run(play_file("background-jazz-SBA-300481063.wav"))
#Guitar
# asyncio.run(play_file("beautiful-romantic-acoustic-guitar-theme-SBA-300271192.wav"))
#Orchestra
# asyncio.run(play_file("meditation-relaxation-SBA-300515820.wav"))
#Precussion
# asyncio.run(play_file("audioblocks-relaxing-lo-fi-chill_HnD1aZd7P.wav"))
#Piano
# asyncio.run(play_file("audioblocks-mountainscape_HvsfghuMI.wav"))
#Synth
# asyncio.run(play_file("nature-life-SBA-300525065.wav"))
#Woodwind
# asyncio.run(play_file("adventures-in-fine-dining-SBA-300514572.wav"))

#####Angry

#Brass
# asyncio.run(play_file("audioblocks-winner-bpm-150-by-cronicbeats-150bpm_SLomsqx5D.wav"))
#Guitar
# asyncio.run(play_file("built-tough-SBA-300540260.wav"))
#Orchestra
# asyncio.run(play_file("fun-dark-stuff-SBA-300172526.wav"))
#Precussion
# asyncio.run(play_file("headbangers-and-mash-SBA-300539836.wav"))
#Piano
# asyncio.run(play_file("audioblocks-down-to-the-wire_BwxESqOtL.wav"))
#Synth
# asyncio.run(play_file("audioblocks-bass-buzzer_SAjmrnuzI.wav"))
#Woodwind
# asyncio.run(play_file("run-SBA-300555157.wav"))

#####Clock
# asyncio.run(play_file("clock-ticking-2.wav"))
# asyncio.run(play_file("clock-ticking-exact-looping_zJ4DdH4_.wav"))
# asyncio.run(play_file("clock-ticking-tick-tock-SBA-300419273.wav"))


############################################################################################


'''
Note: There seems to be a bug somewhere in this code. It occasionally goes out of synch.
ie. even when the song is paused, the LEDs/motors keep going.
How?
'''
