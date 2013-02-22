/*
 * Copyright (c) 2011, 2012 Joseph Gaeddert
 * Copyright (c) 2011, 2012 Virginia Polytechnic Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <complex>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <liquid/liquid.h>

#include <uhd/usrp/multi_usrp.hpp>
 
#include "timer.h"

// global options and parameters
const unsigned int M = 64;              // number of subcarriers
unsigned int cp_len = 8;                // cyclic prefix length
unsigned int taper_len = 0;             // taper length
static bool verbose;                    // verbose output?
const unsigned int num_data_symbols=50; // number of data symbols per frame
modulation_scheme ms = LIQUID_MODEM_QPSK;   // modulation scheme
unsigned char p[M];                     // subcarrier allocation

// global counters
unsigned int num_frames_detected=0;
unsigned int num_symbols_received=0;

// received OFDM symbols
std::complex<float> received_symbols[num_data_symbols][M];

// channel estimate
std::complex<float> H_est[M];

void estimate_channel_and_equalize();
void smooth_channel_estimate();
void demodulate_symbols();

void usage() {
    printf("ofdm_rx -- receive OFDM packets\n");
    printf("  u,h   :   usage/help\n");
    printf("  q/v   :   quiet/verbose\n");
    printf("  f     :   center frequency [Hz]\n");
    printf("  b     :   bandwidth [Hz]\n");
    printf("  G     :   uhd rx gain [dB] (default: 20dB)\n");
    printf("  t     :   run time [seconds]\n");
}

// callback function
int callback(std::complex<float> * _X,
             unsigned char *       _p,
             unsigned int          _M,
             void *                _userdata)
{
    //printf("**** callback invoked!\n");

    if (num_symbols_received==0) {
        num_frames_detected++;
        if (verbose)
            printf("**** frame detected!\n");
    }

    // 
    // DG: add code here for equalization
    //

    // save the OFDM symbols to a giant buffer and process
    // after entire frame has been received
    unsigned int i;
    for (i=0; i<_M; i++)
        received_symbols[num_symbols_received][i] = _X[i];

    // increment symbol counter
    num_symbols_received++;

    // reset frame synchronizer if this is the last symbol
    // in the frame
    if (num_symbols_received == num_data_symbols) {
        // estimate channel now
        estimate_channel_and_equalize();

        // ...
        smooth_channel_estimate();

        // demodulate symbols
        demodulate_symbols();

        // reste everything here
        num_symbols_received = 0;   // reset symbol counter
        return 1;                   // reset the frame synchronizer
    }

    // not at the end of the frame yet; just return zero
    return 0;
}

// estimate channel and equalize
void estimate_channel_and_equalize()
{
    // pilot symbols
    float pilots[8] = {  1.0f, -1.0f, -1.0f, 1.0f,
                        -1.0f,  1.0f, -1.0f, 1.0f };
    unsigned int pilot_index = 0;

    // reset channel estimate
    unsigned int i;
    for (i=0; i<M; i++)
        H_est[i] = 0.0f;

    //
    unsigned int j;
    for (i=0; i<num_data_symbols; i++) {
        for (j=0; j<M; j++) {
            // pilot precoder
            std::complex<float> pilot = pilots[pilot_index];
            pilot_index = (pilot_index + 1) % 8;

            // DG: insert actual code to estimate channel here
            H_est[j] += pilot * received_symbols[i][j];
        }
    }

    // now that we have H_est, equalize
    pilot_index = 0;    // reset pilot index
    for (i=0; i<num_data_symbols; i++) {
        for (j=0; j<M; j++) {
            // pilot precoder
            std::complex<float> pilot = pilots[pilot_index];
            pilot_index = (pilot_index + 1) % 8;

            // DG: insert actual code to equalize symbols
            received_symbols[i][j] = (received_symbols[i][j] - pilot) / H_est[j];
        }
    }
}

void smooth_channel_estimate()
{
    // TODO : copy code to smooth channel estimate
}

// demodulate symbols
void demodulate_symbols()
{
    // create demodulator and pseudo-random number generator
    modem demod = modem_create(ms);
    msequence seq = msequence_create_default(8);

    // demodulate symbol and count errors
    unsigned int bps = modem_get_bps(demod);

    unsigned int num_bit_errors = 0;
    unsigned int total_bits     = 0;
    unsigned int i;
    unsigned int j;
    for (i=0; i<num_data_symbols; i++) {
        for (j=0; j<M; j++) {
            if (p[j] == OFDMFRAME_SCTYPE_DATA) {
                // data subcarrier: generate pseudo-random number and
                // compare to demodulated symbol
                unsigned int sym_tx = msequence_generate_symbol(seq, bps);
                unsigned int sym_rx = 0;    // demodulated symbol

                // demodulate symbols (assuming we have equalized)
                modem_demodulate(demod, received_symbols[i][j], &sym_rx);

                // count errors
                num_bit_errors += count_bit_errors(sym_tx, sym_rx);
                total_bits     += bps;
            }
        }
    }
    
    // print results
    printf("  OFDM frame bit errors: %6u / %6u\n",
        num_bit_errors, total_bits);

    modem_destroy(demod);
    msequence_destroy(seq);
}

int main (int argc, char **argv)
{
    // command-line options
    verbose = false;
    unsigned long int ADC_RATE = 64e6;

    double min_bandwidth = 0.25*(ADC_RATE / 512.0);
    double max_bandwidth = 0.25*(ADC_RATE /   4.0);

    double frequency = 462.0e6;
    double bandwidth = 80e3f;
    double num_seconds = 5.0f;
    double uhd_rxgain = 20.0;

    //
    int d;
    while ((d = getopt(argc,argv,"uhqvf:b:G:t:z:")) != EOF) {
        switch (d) {
        case 'u':
        case 'h':   usage();                        return 0;
        case 'q':   verbose = false;                break;
        case 'v':   verbose = true;                 break;
        case 'f':   frequency = atof(optarg);       break;
        case 'b':   bandwidth = atof(optarg);       break;
        case 'G':   uhd_rxgain = atof(optarg);      break;
        case 't':   num_seconds = atof(optarg);     break;
        default:
            usage();
            return 0;
        }
    }

    if (bandwidth > max_bandwidth) {
        fprintf(stderr,"error: %s, maximum symbol rate exceeded (%8.4f MHz)\n", argv[0], max_bandwidth*1e-6);
        exit(1);
    } else if (bandwidth < min_bandwidth) {
        fprintf(stderr,"error: %s, minimum symbol rate exceeded (%8.4f kHz)\n", argv[0], min_bandwidth*1e-3);
        exit(1);
    } else if (cp_len == 0 || cp_len > M) {
        fprintf(stderr,"error: %s, cyclic prefix must be in (0,M]\n", argv[0]);
        exit(1);
    }

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.stream_now = true;

    uhd::device_addr_t dev_addr;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(dev_addr);

    // set properties
    double rx_rate = 4.0f*bandwidth;
    // NOTE : the sample rate computation MUST be in double precision so
    //        that the UHD can compute its decimation rate properly
    unsigned int decim_rate = (unsigned int)(ADC_RATE / rx_rate);
    // ensure multiple of 2
    decim_rate = (decim_rate >> 1) << 1;
    // compute usrp sampling rate
    double usrp_rx_rate = ADC_RATE / (float)decim_rate;
    
    // try to set rx rate
    usrp->set_rx_rate(ADC_RATE / decim_rate);

    // get actual rx rate
    usrp_rx_rate = usrp->get_rx_rate();

    // compute arbitrary resampling rate
    double rx_resamp_rate = rx_rate / usrp_rx_rate;

    usrp->set_rx_freq(frequency);
    usrp->set_rx_gain(uhd_rxgain);

    printf("frequency   :   %12.8f [MHz]\n", frequency*1e-6f);
    printf("bandwidth   :   %12.8f [kHz]\n", bandwidth*1e-3f);
    printf("verbosity   :   %s\n", (verbose?"enabled":"disabled"));
    printf("sample rate :   %12.8f kHz = %12.8f * %8.6f (decim %u)\n",
            rx_rate * 1e-3f,
            usrp_rx_rate * 1e-3f,
            rx_resamp_rate,
            decim_rate);
    if (num_seconds >= 0)
        printf("run time    :   %f seconds\n", num_seconds);
    else
        printf("run time    :   (forever)\n");

    // add arbitrary resampling block
    resamp_crcf resamp = resamp_crcf_create(rx_resamp_rate,7,0.4f,60.0f,64);
    resamp_crcf_setrate(resamp, rx_resamp_rate);

    unsigned int block_len = 64;
    assert( (block_len % 2) == 0);  // ensure block length is even

    //allocate recv buffer and metatdata
    uhd::rx_metadata_t md;
    const size_t max_samps_per_packet = usrp->get_device()->get_max_recv_samps_per_packet();
    std::vector<std::complex<float> > buff(max_samps_per_packet);

    // half-band decimator
    resamp2_crcf decim = resamp2_crcf_create(7,0.0f,40.0f);

    // initialize subcarrier allocation
    ofdmframe_init_default_sctype(M, p);

    // create frame synchronizer
    ofdmframesync fs = ofdmframesync_create(M,
                                            cp_len,
                                            taper_len,
                                            p,
                                            callback,
                                            NULL);
    ofdmframesync_print(fs);

    // start data transfer
    usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    printf("usrp data transfer started\n");
 
    std::complex<float> data_rx[block_len];
    std::complex<float> data_decim[block_len/2];
    std::complex<float> data_resamp[block_len];
 
    // run conditions
    int continue_running = 1;
    timer t0 = timer_create();
    timer_tic(t0);

    unsigned int n=0;
    while (continue_running) {
        // grab data from port
        size_t num_rx_samps = usrp->get_device()->recv(
            &buff.front(), buff.size(), md,
            uhd::io_type_t::COMPLEX_FLOAT32,
            uhd::device::RECV_MODE_ONE_PACKET
        );

        //handle the error codes
        switch(md.error_code){
        case uhd::rx_metadata_t::ERROR_CODE_NONE:
        case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
            break;

        default:
            std::cerr << "Error code: " << md.error_code << std::endl;
            std::cerr << "Unexpected error on recv, exit test..." << std::endl;
            return 1;
        }

        // for now copy vector "buff" to array of complex float
        // TODO : apply bandwidth-dependent gain
        unsigned int j;
        unsigned int nw=0;
        for (j=0; j<num_rx_samps; j++) {
            // push samples into buffer
            data_rx[n++] = buff[j];

            if (n==block_len) {
                // reset counter
                n=0;

                // decimate to block_len/2
                unsigned int k;
                for (k=0; k<block_len/2; k++)
                    resamp2_crcf_decim_execute(decim, &data_rx[2*k], &data_decim[k]);

                // apply resampler
                for (k=0; k<block_len/2; k++) {
                    resamp_crcf_execute(resamp, data_decim[k], &data_resamp[n], &nw);
                    n += nw;
                }

                // push through synchronizer
                ofdmframesync_execute(fs, data_resamp, n);

                // reset counter (again)
                n = 0;
            }
        }

        // check runtime
        if (timer_toc(t0) >= num_seconds)
            continue_running = 0;
    }
 
    // compute actual run-time
    float runtime = timer_toc(t0);

    // stop data transfer
    usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
    printf("\n");
    printf("usrp data transfer complete\n");
 
    // print results
    printf("    frames detected     : %6u\n", num_frames_detected);
    printf("    run time            : %f s\n", runtime);

    // destroy objects
    resamp_crcf_destroy(resamp);
    resamp2_crcf_destroy(decim);
    ofdmframesync_destroy(fs);
    timer_destroy(t0);

    return 0;
}
