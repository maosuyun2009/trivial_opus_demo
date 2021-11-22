#include <errno.h>
#include <opus/opus.h>
#include <opus/opus_defines.h>
#include <opus/opus_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SIZE 160
#define SAMPLE_RATE 16000
#define CHANNELS 1
#define BITRATE 32000

#define MAX_FRAME_SIZE (6 * 960)
#define MAX_PACKET_SIZE (3 * 1276)

#define check_encoder_option(decode_only, opt) do {if (decode_only) {fprintf(stderr, "option %s is only for encoding\n", opt); goto failure;}} while (0)

void print_usage(char *argv[])
{
    fprintf(stderr, "Usage: %s [-e] <application> <sampling rate (Hz)> <channels (1/2) "
        "<bits per second> [options] <input> <output>\n", argv[0]);
    fprintf(stderr, "       %s -d <sampling rate (Hz)> <channels (1/2)> <packet size> "
        "[optinos] <input> <output>\n\n", argv[0]);
    fprintf(stderr, "application: voip | audio | restricted-lowdelay\n" );
    fprintf(stderr, "options:\n" );
    fprintf(stderr, "-e                   : only runs the encoder (output the bit-stream)\n" );
    fprintf(stderr, "-d                   : only runs the decoder (reads the bit-stream as input)\n" );
    fprintf(stderr, "-delayed-decision    : use look-ahead for speech/music detection (experts only); default: disabled\n" );
    fprintf(stderr, "-bandwidth <NB|MB|WB|SWB|FB> : audio bandwidth (from narrowband to fullband); default: sampling rate\n" );
    fprintf(stderr, "-framesize <2.5|5|10|20|40|60|80|100|120> : frame size in ms; default: 20 \n" );
    fprintf(stderr, "-max_payload <bytes> : maximum payload size in bytes, default: 1024\n" );
    fprintf(stderr, "-complexity <comp>   : complexity, 0 (lowest) ... 10 (highest); default: 10\n" );
    fprintf(stderr, "-inbandfec           : enable SILK inband FEC\n" );
    fprintf(stderr, "-forcemono           : force mono encoding, even for stereo input\n" );
    fprintf(stderr, "-dtx                 : enable SILK DTX\n" );
    fprintf(stderr, "-loss <perc>         : simulate packet loss, in percent (0-100); default: 0\n" );
}

int main(int argc, char **argv) 
{
    int args;
    char *inFile;
    FILE *fin;
    char *outFile;
    FILE *fout;
    int application = OPUS_APPLICATION_AUDIO;
    int encode_only = 0, decode_only = 0;
    opus_int16 *in = NULL;
    opus_int16 *out = NULL;
    unsigned char cbits[MAX_PACKET_SIZE];
    int nBytes;
    opus_int32 sampling_rate;
    opus_int32 bitrate_bps=0;
    opus_int32 pkt_size = 0;
    int frame_size, channels;
    int use_vbr;
    int max_payload_bytes;
    int complexity;
    int use_inbandfec;
    int use_dtx;
    int forcechannels;
    int packet_loss_perc;
    int bandwidth=OPUS_AUTO;
    const char *bandwidth_string;
    int max_frame_size = 48000*2;

    OpusEncoder *encoder;
    OpusDecoder *decoder;
    int err;
    int ret = EXIT_FAILURE;

    if (argc < 5) {
        print_usage(argv);
        return EXIT_FAILURE;
    }

    args = 1;
    if (strcmp(argv[args], "-e") == 0)
    {
        encode_only = 1;
        args++;
    }
    else if (strcmp(argv[args],  "-d") == 0)
    {
        decode_only = 1;
        args++;
    }
    if (!decode_only && argc < 7) {
        print_usage(argv);
        return EXIT_FAILURE;
    }

    if (!decode_only)
    {
        if (strcmp(argv[args], "voip") == 0)
            application = OPUS_APPLICATION_VOIP;
        else if (strcmp(argv[args], "restricted-lowdelay") == 0)
            application = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
        else if (strcmp(argv[args], "audio") != 0)
        {
            fprintf(stderr, "unknown application: %s\n", argv[args]);
            print_usage(argv);
            goto failure;
        }
        args++;
    }

    sampling_rate = (opus_int32)atol(argv[args]);
    args++;

    if (sampling_rate != 8000 && sampling_rate != 12000 && sampling_rate != 16000 &&
        sampling_rate != 24000 && sampling_rate != 48000)
    {
        fprintf(stderr, "Sampling rate must be one of 8000, 12000, 16000, 24000, 48000\n");
        goto failure;
    }
    frame_size = sampling_rate / 50;

    channels = atoi(argv[args]);
    args++;

    if (channels < 1 || channels > 2)
    {
        fprintf(stderr, "Channels must be 1 or 2\n");
        goto failure;
    }

    if (!encode_only)
    {
        pkt_size = (opus_int32)atol(argv[args]);
        args++;
    }

    if (!decode_only)
    {
        bitrate_bps = (opus_int32)atol(argv[args]);
        args++;
    }

    /* default: */
    use_vbr = 0;
    max_payload_bytes = MAX_PACKET_SIZE;
    complexity = 0;
    use_inbandfec = 0;
    forcechannels = OPUS_AUTO;
    use_dtx = 0;
    packet_loss_perc = 0;

    while (args < argc - 2)
    {
        if (strcmp(argv[args], "-bandwidth") == 0)
        {
            check_encoder_option(decode_only, "-bandwidth");
            if (strcmp(argv[args + 1], "NB") == 0)
                bandwidth = OPUS_BANDWIDTH_NARROWBAND;
            else if (strcmp(argv[args + 1], "MB") == 0)
                bandwidth = OPUS_BANDWIDTH_MEDIUMBAND;
            else if (strcmp(argv[args + 1], "WB") == 0)
                bandwidth = OPUS_BANDWIDTH_WIDEBAND;
            else if (strcmp(argv[args + 1], "SWB") == 0)
                bandwidth = OPUS_BANDWIDTH_SUPERWIDEBAND;
            else if (strcmp(argv[args + 1], "FB") == 0)
                bandwidth = OPUS_BANDWIDTH_FULLBAND;
            else {
                fprintf(stderr, "Unknown bandwidth: %s."
                                "Supported are NB, MB, WB, SWB, FB.\n", 
                                argv[args + 1]);

                goto failure;
            }
            args += 2;
        }
        else if (strcmp(argv[args], "-framesize") == 0)
        {
            check_encoder_option(decode_only, "-framesize");
            if (strcmp(argv[args + 1], "2.5") == 0)
                frame_size = sampling_rate / 400;
            else if (strcmp(argv[args + 1], "5") == 0)
                frame_size = sampling_rate / 200;
            else if (strcmp(argv[args + 1], "10") == 0)
                frame_size = sampling_rate / 100;
            else if (strcmp(argv[args + 1], "20") == 0)
                frame_size = sampling_rate / 50;
            else if (strcmp(argv[args + 1], "40") == 0)
                frame_size = sampling_rate / 25;
            else if (strcmp(argv[args + 1], "60") == 0)
                frame_size = 3*sampling_rate / 50;
            else if (strcmp(argv[args + 1], "80") == 0)
                frame_size = 4*sampling_rate / 50;
            else if (strcmp(argv[args + 1], "100") == 0)
                frame_size = 5*sampling_rate / 50;
            else if (strcmp(argv[args + 1], "120") == 0)
                frame_size = 6*sampling_rate / 50;
            else {
                fprintf(stderr, "Unknown frame size: %s."
                                "Supported are 2.5, 5, 10, 20, 40, 60, 80, 100, 120.\n", 
                                argv[args + 1]);

                goto failure;
            }
            args += 2;
        }
        else if (strcmp(argv[args], "-max_payload") == 0)
        {
            check_encoder_option(decode_only, "-max_payload");
            args += 2;
        }
        else if (strcmp(argv[args], "-complexity") == 0)
        {
            check_encoder_option(decode_only, "-complexity");
            complexity = atoi(argv[args + 1]);
            args += 2;
        }
        else if ( strcmp( argv[ args ], "-inbandfec" ) == 0){
            use_inbandfec = 1;
            args++;
        } else if( strcmp( argv[ args ], "-forcemono" ) == 0 ) {
            check_encoder_option(decode_only, "-forcemono");
            forcechannels = 1;
            args++;
        } else if( strcmp( argv[ args ], "-dtx") == 0 ) {
            check_encoder_option(decode_only, "-dtx");
            use_dtx = 1;
            args++;
        } else if( strcmp( argv[ args ], "-loss" ) == 0 ) {
            packet_loss_perc = atoi( argv[ args + 1 ] );
            args += 2;
        } else {
            printf( "Error: unrecognized setting: %s\n\n", argv[ args ] );
            print_usage( argv );
            goto failure;
        }
    }

    if (max_payload_bytes < 0 || max_payload_bytes > MAX_PACKET_SIZE)
    {
        fprintf (stderr, "max_payload_bytes must be between 0 and %d\n",
                          MAX_PACKET_SIZE);
        goto failure;
    }

    inFile = argv[argc - 2];
    fin = fopen(inFile, "r");
    if (fin == NULL) {
        fprintf(stderr, "failed to open input file: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    outFile = argv[argc - 1];
    fout = fopen(outFile, "wb");
    if (fout == NULL) {
        fprintf(stderr, "failed to open output file: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (!decode_only)
    {
        encoder =
            opus_encoder_create(sampling_rate, channels, application, &err);
        if (err != OPUS_OK) {
            fprintf(stderr, "failed to create an encoder: %s\n", opus_strerror(err));
            return EXIT_FAILURE;
        }

        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate_bps));
        opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH(bandwidth));
        opus_encoder_ctl(encoder, OPUS_SET_VBR(use_vbr));
        opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(0));
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(complexity));
        opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(use_inbandfec));
        opus_encoder_ctl(encoder, OPUS_SET_FORCE_CHANNELS(forcechannels));
        opus_encoder_ctl(encoder, OPUS_SET_DTX(use_dtx));
        opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(packet_loss_perc));
        opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));
        opus_encoder_ctl(encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_ARG));

    }

    if (!encode_only)
    {
        decoder = opus_decoder_create(sampling_rate, channels, &err);
        if (err < OPUS_OK) {
            fprintf(stderr, "failed to create decoder: %s\n", opus_strerror(err));
            return EXIT_FAILURE;
        }

    }

    switch(bandwidth)
    {
    case OPUS_BANDWIDTH_NARROWBAND:
         bandwidth_string = "narrowband";
         break;
    case OPUS_BANDWIDTH_MEDIUMBAND:
         bandwidth_string = "mediumband";
         break;
    case OPUS_BANDWIDTH_WIDEBAND:
         bandwidth_string = "wideband";
         break;
    case OPUS_BANDWIDTH_SUPERWIDEBAND:
         bandwidth_string = "superwideband";
         break;
    case OPUS_BANDWIDTH_FULLBAND:
         bandwidth_string = "fullband";
         break;
    case OPUS_AUTO:
         bandwidth_string = "auto bandwidth";
         break;
    default:
         bandwidth_string = "unknown";
         break;
    }

    if (decode_only)
       fprintf(stderr, "Decoding with %ld Hz output (%d channels)\n",
                       (long)sampling_rate, channels);
    else
       fprintf(stderr, "Encoding %ld Hz input at %.3f kb/s "
                       "in %s with %d-sample frames.\n",
                       (long)sampling_rate, bitrate_bps*0.001,
                       bandwidth_string, frame_size);

    in = (short *)malloc(max_frame_size*channels*sizeof(short));
    out = (short *)malloc(max_frame_size*channels*sizeof(short));

    unsigned char *fbytes = malloc(max_frame_size * channels * sizeof(short));
    while (1) {
        int i;

        if (!decode_only)
        {
            fread(fbytes, sizeof(short) * channels, frame_size, fin);
            if (feof(fin)) {
                break;
            }

            //short in  = (short *)pcm_bytes;
            for (i = 0; i < channels * frame_size; i++)
                in[i] = fbytes[2 * i + 1] << 8 | fbytes[2 * i];

            nBytes = opus_encode(encoder, in, frame_size, (unsigned char *)out, max_payload_bytes);
            printf("encode byte: %d\n", nBytes);
            if (nBytes < 0) {
                fprintf(stderr, "Encoding failed: %s\n", opus_strerror(nBytes));
                goto failure;
            }
            fwrite(out, 1, nBytes, fout);
        }
        else
        {
            fread(fbytes, 1, pkt_size, fin);
            if (feof(fin)) {
                break;
            }

            frame_size = opus_decode(decoder, fbytes, pkt_size, out, max_payload_bytes, 0);
            if (frame_size < 0)
            {
                fprintf(stderr, "decoder failed: %s\n", opus_strerror(frame_size));
                goto failure;
            }
            for (i = 0; i < CHANNELS * frame_size; i++) {
                fbytes[2 * i] = out[i] & 0xFF;
                fbytes[2 * i + 1] = (out[i] >> 8) & 0xFF;
            }
            fwrite(fbytes, sizeof(short), frame_size * channels, fout);
        }
    }
    ret = EXIT_SUCCESS;

failure:
    if (!decode_only)
        opus_encoder_destroy(encoder);
    if (!encode_only)
        opus_decoder_destroy(decoder);
    if (fin)
        fclose(fin);
    if (fout)
        fclose(fout);
    free(in);
    free(out);
    free(fbytes);

    return ret;
}
