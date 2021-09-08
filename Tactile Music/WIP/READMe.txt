------------------ Problem Statement -----------------------
- Design a 3 Band Equalizer that allows user to choose one (or a combination) of the following:
	* Low Pass Filter
	* High Pass Filter
	* Band Stop Filter
- Both audio and the 3 LEDs should produce the filtered output based on the user choice.
- The LEDs should blink in correspondance/sync to the audio output, with an LED corresponding to Low freq, Mid freq and High freq each.

------------------ Environment -----------------------
- The DSK6713 DSP Board was used to implement this project.
- 3 DIP switches, 3 LEDS and the audio codec were utilized.
- Embedded Direct Memory Access (EDMA) was used for data storage & several segments of the code are dedicated to setting up the EDMA. These can be ignored for implementation on a different board.

------------------ Our Solution (---processBuffer() function---)  ---------------------
#1. Filters

- Since the change in the visual bands (LEDs) does not need to be as accurate as the audio output , 2 sets of filters were designed.
- 3 13 order filters (LPF, BPF, HPF) for LEDs and,
- 3 101 order filters for the audio channels.

- The idea was that since lower order filters require less compuation and since our eyes can't detect faster changes, we could trade-off the resolution for less computation.

#2. Convolution & Transfer Function (h)

- Through a mathematical process called convolution any input signal can be transformed into a different output signal. 
- The model used to transform the signal is known as the transfer function. Transfer function is usually represented by H or h.
- So, essentially convolving the input signal with the transfer function (also known as impulse response in time domain) gives the output signal.
- In the case of audio signal processing, the input is the audio signal and the transfer function is the filter used.

- Convolution is basically performed by multiplying the audio samples (eg. 101) with the 101 filter coefficient and then additing all the products together to get one output sample. 
- To get the next output sample, the next 101 inputs are multiplied with the filter coeffs. i.e. the filter is slowed moved (shifted) through the input till the end.
- Please note that, in convolution, either the audio signal or the filter needs to be flipped vertically before multiplying.
- ie. if the filter coefficients are being processed moving forward (increment) then the audio samples need to be processed moving backwards/flipped (decrement).

#3. DIP switches
- Since there are 3 switches, total 8 combinations were possible.
- x, y, z represent the ON/OFF stage of the 3 switches.
- case 0 (000): no pass => no output
- case 1 (001): LPF
- case 2 (010): BPF
- case 3 (011): LPF + BPF
- case 4 (100): HPF
- case 5 (101): HPF + LPF (or Band Stop Filter)
- case 6 (110): HPF + BPF
- case 7 (111): All pass => same as input

#4. PING - PONG buffers
- 2 buffers were created of 1kB length each.
- One was used for storing/collecting the incoming audio samples (PING),
- While data processing occurs on the other one (PONG).
- Once convolution/processing from the PONG is completed, 
- then processing starts on PING while new samples get stored on PONG.
- This is done to ensure no new audio samples are lost while the processing takes place.

#5. LED display
- Since each LED was supposed to represent its own frequency band (LP, BP, HP), 
the 3 computations were done seperately.
- The LED intensity is detemined by the avg. power of the filtered output,
for the entire 1kB buffer samples.

- 1. convolve the 13 filter coeffs with the audio signal
- 2. determine the total power of the output samples by squaring each sample & adding them up.
- 3. find the average power based on the total number of samples.

***** blinkLED() ****
- The LEDs are updated every 500ms.

- Only when the power was above a certain threshold, the LEDs would light up..
- Thresholds were emperically detemined based on how the filtered sound (eg. LPF output) and the LPF LED rythm matched.

- Apparently, the chosen thresholds were:
-- LPF: 800,000
-- BPF: 400,000
-- HPF: 125










