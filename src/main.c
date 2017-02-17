#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <glib.h>

#include <lcm/lcm.h>
#include <camunits/cam.h>

#include <bot_core/bot_core.h>
#include <bot_param/param_client.h>
#include <hr_common/path_util.h>

//#include <lcmtypes/lcmtypes.h>

#define UTIL_DBG 1
#define UTIL_LOG 1
#include "util.h"

// This is one logical stream of images to send an image on LCM.
typedef struct
{
    char *key_name;
    char *channel;
    int hz;
    int width;
    int height;
    int jpeg_quality;
} stream_t;

static void stream_free(stream_t *self)
{
    if (!self) return;
    free(self->key_name);
    free(self->channel);
    free(self);
}

#define STREAM_GET_PARAM(__param, __type, __rtn, __kf, ...) do { \
    snprintf(sub_key, 64, __kf, ##__VA_ARGS__); \
    if (bot_param_get_##__type(__param, sub_key, __rtn) < 0) { \
        ERR("No %s value in conf file!  Could not add %s stream.\n", \
                sub_key, key); \
        stream_free(self); \
        return NULL; \
    } \
} while (0)

static stream_t *stream_new_from_param_entry(BotParam *param, const char *key)
{
    DBG("Creating new stream with key \"%s\"\n", key);
    char sub_key[64];

    stream_t *self = (stream_t*)calloc(1, sizeof(stream_t));

    STREAM_GET_PARAM(param, int, &self->hz,     "%s.hz",     key);
    STREAM_GET_PARAM(param, int, &self->width,  "%s.width",  key);
    STREAM_GET_PARAM(param, int, &self->height, "%s.height", key);

    snprintf(sub_key, 64, "%s.jpeg_quality", key);
    if (bot_param_get_int (param, sub_key, &(self->jpeg_quality)) == -1) {
        self->jpeg_quality = 75;
        fprintf (stdout, "Configuration %s not found. Using jpeg_quality of %d\n",
                 sub_key, self->jpeg_quality);
    }
    //self->jpeg_quality = bot_param_get_int_or_default(param, sub_key, 75);

    // Now the strings (saved for last to save time in case of error.
    char *channel;
    STREAM_GET_PARAM(param, str, &channel, "%s.channel", key);
    self->channel = strdup(channel);
    self->key_name = strdup(key);

    return self;
}

#undef STREAM_GET_PARAM

static inline void stream_print_info(const stream_t *self)
{
    printf("\"%s\" stream publishing (%d,%d)@%dHz with %d quality on \"%s\"\n",
            self->key_name, self->width, self->height,
            self->hz, self->jpeg_quality, self->channel);
}

typedef struct
{
    lcm_t *lcm;

    BotParam *param;

    CamUnitManager *manager;
    CamUnitChain *source;
    GSList *streams;

    char *cam_name;

    // Checking signals from n-gui
    gboolean verbose;
    gboolean simulated;

    int64_t img_recv_last_utime;
    double img_scale;
} state_t;

// Determines if the CAMUNITS_PLUGIN_PATH environment variable was set.
static inline gboolean camunits_plugin_path_set()
{
    char *camunits = getenv("CAMUNITS_PLUGIN_PATH");
    return camunits && strlen(camunits) > 0;
}

static void usage(const char *name, BotParam *param)
{
    printf("%s: Publish camera images to LCM.\n"
           "\n"
           "    Usage: %s (-c|-s) <cam-name> [-v]\n"
           "\n"
           "    Options:\n"
           "        -c --camera=<cam-name>\n"
           "            Connect to specified camera.  Read agile.cfg for\n"
           "            info on resizing, rate, and LCM channel.\n"
           "        -s --sim-cam=<cam-name>\n"
           "            Start simulation of specified camera.  Read agile.cfg\n"
           "            for info on resizing, rate, and LCM channel.\n"
           "        -o --omit=<csv>\n"
           "            Omit all camera streams with the names in the comma-\n"
           "            separated list of keys.  If a key does not exist, this\n"
           "            a warning will be generated.\n"
           "        -v --verbose\n"
           "            Run verbosely.\n"
           "        -h --help\n"
           "            Print out this help and exit.\n"
           "\n"
           "", name, name);

    //char **cameras = bot_param_get_subkeys(param, "cameras");
    //printf("    <cam-name> can be any one of:\n");
    //for (int i = 0; cameras[i]; i++)
    //    printf("        %s\n", cameras[i]);
    //printf("\n");
    //g_strfreev(cameras);
}


static void on_frame_ready(CamUnit *unit, CamFrameBuffer *fbuf,
        CamUnitFormat *fmt, gpointer user)
{
    stream_t *stream = (stream_t*)user;
    printf("(%s) Publishing on \"%s\"\n", stream->key_name, stream->channel);
}

void parse_args_and_run(state_t *self, int argc, char *argv[])
{

}



////////////////////////////////////////////////////////////////////////////////
// ------------------------------ Main/State -------------------------------- //
////////////////////////////////////////////////////////////////////////////////

static void state_free(state_t *self)
{
    if (!self) return;
    for (GSList *iter = self->streams; iter; iter = iter->next)
        stream_free(iter->data);
    g_slist_free(self->streams);
    free(self);
}

#define ASSERT(__val, __msg, ...) do { \
    if (!(__val)) { \
        ERR(__msg, ##__VA_ARGS__); \
        state_free(self); \
        return NULL; \
    } \
} while (0)

#undef ASSERT

int main (int argc, char *argv[])
{
    setlinebuf(stdout);



    g_type_init();

    //LOG_OPEN("camera.log");

    state_t *self = (state_t *) calloc (1, sizeof (state_t));
    
    char *optstring = "hvc:s:o:";
    struct option long_opts[] =
    {
        { "help",    no_argument, NULL, 'h' },
        { "verbose", no_argument, NULL, 'v' },

        // Cameras:
        { "camera",  required_argument, NULL, 'c' },
        { "sim-cam", required_argument, NULL, 's' },
        { "omit",    required_argument, NULL, 'o' },
        { 0, 0, 0, 0 }
    };

    char **stream_keys = NULL;
    char *cam_key = NULL;
    char *key = NULL;
    char *rtn = NULL;

    char *file_name = NULL;

    gchar **omits = NULL;

    int c;
    DBG("Parsing args.\n");
    while ((c = getopt_long(argc, argv, optstring, long_opts, 0)) >= 0)
    {
        switch (c)
        {
            case '?': return;

            // Minor alterations.
            case 'v':
                fprintf(stdout, "Got 'v'\n");
                self->verbose = TRUE;
                break;
            case 'o':
                fprintf(stdout, "Omitting: %s\n", optarg);
                omits = g_strsplit(optarg, ",", 0);
                break;

            // Camera selection.
            case 's':
                fprintf (stdout, "Simulated camera mode currently not supported\n");
                camera_help (argv[0], self->param);
                return;
                self->simulated = TRUE;
                self->cam_name = optarg;
                //LOG("Got camera parameter \"%s\".\n", optarg);
                break;
            case 'c':
                self->cam_name = optarg;
                //LOG("Got camera parameter \"%s\".\n", optarg);
                break;

            default:
                fprintf(stderr, "Unrecognized option???\n");
            case 'h':
                printf("Help message.\n");
                camera_help(argv[0], self->param);
                return;
        }
    }

    self->lcm = bot_lcm_get_global (NULL);
    bot_glib_mainloop_attach_lcm (self->lcm);

    // Instantiate the BotParam client
    self->param = bot_param_new_from_server (self->lcm, 0);


    if (self->cam_name)
    {
        // cam_key = "cameras.#{optarg}"
        cam_key = (char*)calloc(strlen(self->cam_name) + 17, sizeof(char));
        snprintf(cam_key, strlen(self->cam_name) + 17, "cameras.%s.streams", self->cam_name);

        // Get all the sub-keys.
        stream_keys = bot_param_get_subkeys(self->param, cam_key);

        if (!stream_keys)
        {
            ERR("Could not find configuration for \"%s\"\n", cam_key);
            return;
        }

        // Remove all keys that are in the ?? - wht matt???
        guint stream_keys_len = g_strv_length(stream_keys);
        for (int i = 0; omits && omits[i]; i++)
        {
            int j = 0;
            while (stream_keys[j])
            {
                if (!strcmp(stream_keys[j], omits[i]))
                {
                    // XXX Is this safe to do?  Mem. management okay?
                    stream_keys[j][0] = '\0';
                    break;
                }
                j++;
            }

            if (j >= stream_keys_len)
                WRN("Unrecognized key: %s\n", omits[i]);
        }
        g_strfreev(omits);

        // Build stream_t objects for each stream.
        for (int i = 0; stream_keys[i]; i++)
        {
            // This is something we were asked to omit.
            if (stream_keys[i][0] == '\0') continue;

            char key[128];
            memset(key, 0, 128 * sizeof(char));
            snprintf(key, 127, "%s.%s", cam_key, stream_keys[i]);
            if (bot_param_get_num_subkeys(self->param, key) > 0)
            {
                stream_t *stream = stream_new_from_param_entry(self->param, key);
                if (stream)
                    self->streams = g_slist_prepend(self->streams, stream);
            }
        }
        g_strfreev(stream_keys);

        if (!self->streams)
        {
            printf("No streams to stream!  Aborting.\n");
            return;
        }

        // And finally, the XML file containing the camunits source
        // chain (the one connecting to the camera).
        if (self->simulated)
        {
            key = (char*)calloc(strlen(cam_key) + 19, sizeof(char));
            sprintf(key, "%s.cam_units_sim_xml", cam_key);
        }
        else
        {
            key = (char*)calloc(strlen(cam_key) + 15, sizeof(char));
            sprintf(key, "%s.cam_units_xml", cam_key);
        }

        if (bot_param_get_str(self->param, key, &rtn))
        {
            fprintf(stderr, "Could not get cam_units_xml.\n");
            return;
        }

        const char *config_dir = getConfigPath();
        file_name =
            (char*)calloc(strlen(rtn) +
                          strlen(config_dir) + 2, sizeof(char));
        sprintf(file_name, "%s/%s", config_dir, rtn);
        
        free(cam_key);
        free(rtn);
    }


    if (self->simulated && !camunits_plugin_path_set())
    {
        printf("CAMUNITS_PLUGIN_PATH environment variable is not set.  Set it and rerun this.\n");
        return;
    }

    if (self->verbose)
    {
        printf("Parsed arguments and got:\n");
        for (GSList *iter = self->streams; iter; iter = iter->next)
            stream_print_info((stream_t*)iter->data);
    }

    if (!self->cam_name)
    {
        fprintf(stderr, "No camera input specified!\n");
        return;
    }

    // Create the chain from the contents of a file.
    VRB("Creating chain with initial input from file %s ....\n", file_name);

    gsize length = 3000;
    gchar *xml = NULL;
    GError *error = NULL;
    g_file_get_contents(file_name, &xml, &length, &error);
    if (error)
    {
        ERR("Could not get contents of file: %s\n%s\n", file_name, error->message);
        g_error_free(error);
        free(file_name);
        return;
    }
    free(file_name);

    // Create the image processing chain.
    VRB("Creating CamUnitChain.\n");
    self->source = cam_unit_chain_new();

    DBG("Loading XML.\n");
    do // ... and load it with the XML string.
    {
        cam_unit_chain_load_from_str(self->source, xml, &error);
        if (error)
        {
            g_error_free(error);
            fprintf(stderr, " Waiting to try again");
            for (int i = 0; i < 5; i++)
            {
                fprintf(stderr, " .");
                fflush(stderr);
                sleep(1);
            }
            fprintf(stderr, "\n");
        }
    }
    while (error);

    DBG("Freeing XML.\n");
    free(xml);

    // Check for errors.
    CamUnit *faulty_unit = NULL;
    while ((faulty_unit = cam_unit_chain_all_units_stream_init(self->source)))
    {
        ERR("Unit [%s] is not streaming.  Waiting...\n",
                cam_unit_get_name(faulty_unit));
        sleep(5);
    }

    GList *units = cam_unit_chain_get_units(self->source);
    if (units)
    {
        // TODO remove this after the camunits bug has been fixed.
        CamUnit * input_unit = CAM_UNIT(units->data);
        const char *id = cam_unit_get_id(input_unit);
        if(!strncmp(id, "input.dc1394", strlen("input.dc1394")))
        {
            cam_unit_set_control_int(input_unit, "packet-size", 1000);
        }
    }
    g_list_free(units);

    // Get a handle on the manager, so we can build a tree.
    self->manager = cam_unit_manager_get_and_ref();
    CamUnit *last = cam_unit_chain_get_last_unit(self->source);

    // Loop through each stream and add a camchain for it.
    for (GSList *iter = self->streams; iter; iter = iter->next)
    {
        stream_t *stream = (stream_t*)iter->data;
        CamUnit *throttle, *resize, *compress, *output;

#       define CUM_CREATE(__rtn__,__name__) do { \
            if (!(__rtn__ = cam_unit_manager_create_unit_by_id( \
                            self->manager, __name__))) { \
                ERR("Could not create %s unit for %s stream!\n", \
                        __name__, stream->key_name); \
                return; \
            } \
        } while (0)

        CUM_CREATE(throttle, "util.throttle");
        cam_unit_set_control_boolean(throttle, "pause", 0);
        cam_unit_set_control_boolean(throttle, "repeat", 0);
        cam_unit_set_control_enum(throttle, "throttle-mode", 1);
        cam_unit_set_control_float(throttle, "throttle-rate", (float)stream->hz);
        cam_unit_set_input(throttle, last);
        cam_unit_stream_init(throttle, NULL);
        DBG("(%s) Successfully created throttle.\n", stream->key_name);

        CUM_CREATE(resize, "ipp.resize");
        cam_unit_set_control_boolean(resize, "lock-aspect", 0);
        cam_unit_set_control_int(resize, "width", stream->width);
        cam_unit_set_control_int(resize, "height", stream->height);
        cam_unit_set_input(resize, throttle);
        cam_unit_stream_init(resize, NULL);
        DBG("(%s) Successfully created resize.\n", stream->key_name);

        //// [MW] Framewave seems to pixelate on compression
        ////CUM_CREATE(compress, "framewave.compress");
        CUM_CREATE(compress, "convert.jpeg_compress");
        cam_unit_set_control_int(compress, "quality", stream->jpeg_quality);
        cam_unit_set_input(compress, resize);
        cam_unit_stream_init(compress, NULL);
        DBG("(%s) Successfully created JPEG compress.\n", stream->key_name);

        CUM_CREATE(output, "lcm.image_publish");
        cam_unit_set_control_boolean(output, "publish", 1);
        cam_unit_set_control_string(output, "channel", stream->channel);
        cam_unit_set_control_string(output, "data-rate", 0);
        cam_unit_set_input(output, compress);
        cam_unit_stream_init(output, NULL);
        DBG("(%s) Successfully created LCM image publish - %s.\n", stream->key_name, stream->channel);

        if (self->verbose)
            g_signal_connect(G_OBJECT(output), "frame-ready",
                    G_CALLBACK(on_frame_ready), stream);

#       undef CUM_CREATE
    }

    // Attach chain to glib main loop
    cam_unit_chain_attach_glib(self->source, G_PRIORITY_DEFAULT, NULL);
    GMainLoop *mainloop = g_main_loop_new(NULL, FALSE);
    if (mainloop)
    {
        if (bot_signal_pipe_glib_quit_on_kill(mainloop))
        {
            ERR("Failed to set signal handler to quit main "
                    "loop upon terminating signals\n");
        }
        else
        {
            VRB("Camera is streaming!\n");
            g_main_loop_run(mainloop);
        }

        VRB("\nCamera was shut down.\n");
        g_main_loop_unref(mainloop);
    }

    /* if (self) { */
    /*     parse_args_and_run(self, argc, argv); */
    /*     state_free(self); */
    /* } */

    state_free (self);

    //LOG_CLOSE();
    return 0;
}
