/*
 * Copyright (C) 2011-2014 Karlsruhe Institute of Technology
 *
 * This file is part of ufo-launch.
 *
 * runjson is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * runjson is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with runjson.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <ufo/ufo.h>
#include "ufo/compat.h"


typedef struct {
    char *name;
    GList *props;
} TaskDescription;


static gboolean
str_to_boolean (const gchar *s)
{
    return g_ascii_strncasecmp (s, "true", 4) == 0;
}

#define DEFINE_CAST(suffix, trans_func)                 \
static void                                             \
value_transform_##suffix (const GValue *src_value,      \
                         GValue       *dest_value)      \
{                                                       \
  const gchar* src = g_value_get_string (src_value);    \
  g_value_set_##suffix (dest_value, trans_func (src));  \
}

DEFINE_CAST (uchar,     atoi)
DEFINE_CAST (int,       atoi)
DEFINE_CAST (long,      atol)
DEFINE_CAST (uint,      atoi)
DEFINE_CAST (ulong,     atol)
DEFINE_CAST (float,     atof)
DEFINE_CAST (double,    atof)
DEFINE_CAST (boolean,   str_to_boolean)


static GList *
tokenize_args (int n, char* argv[])
{
    enum {
        NEW_TASK,
        NEW_PROP,
    } state = NEW_TASK;
    GList *tasks = NULL;
    TaskDescription *current;

    for (int i = 0; i < n; i++) {
        if (state == NEW_TASK) {
            current = g_new0 (TaskDescription, 1);
            current->name = argv[i];
            tasks = g_list_append (tasks, current);
            state = NEW_PROP;
        }
        else {
            if (!g_strcmp0 (g_strstrip (argv[i]), "!"))
                state = NEW_TASK;
            else
                current->props = g_list_append (current->props, argv[i]);
        }
    }

    return tasks;
}

static UfoTaskGraph *
parse_pipeline (GList *pipeline, UfoPluginManager *pm, GError **error)
{
    GList *it;
    GRegex *assignment;
    UfoTaskGraph *graph;
    UfoTaskNode *prev = NULL;

    assignment = g_regex_new ("\\s*([A-Za-z0-9-]*)=(.*)\\s*", 0, 0, error);

    if (*error)
        return NULL;

    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_UCHAR,   value_transform_uchar);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_INT,     value_transform_int);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_UINT,    value_transform_uint);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_LONG,    value_transform_long);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_ULONG,   value_transform_ulong);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_FLOAT,   value_transform_float);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_DOUBLE,  value_transform_double);
    g_value_register_transform_func (G_TYPE_STRING, G_TYPE_BOOLEAN, value_transform_boolean);

    graph = UFO_TASK_GRAPH (ufo_task_graph_new ());

    g_list_for (pipeline, it) {
        GList *jt;
        GMatchInfo *match;
        TaskDescription *desc;
        UfoTaskNode *task;

        desc = (TaskDescription *) (it->data);
        task = ufo_plugin_manager_get_task (pm, desc->name, error);

        if (task == NULL)
            return NULL;

        g_list_for (desc->props, jt) {
            gchar *prop_assignment;

            prop_assignment = (gchar *) jt->data;
            g_regex_match (assignment, prop_assignment, 0, &match);

            if (g_match_info_matches (match)) {
                GParamSpec *pspec;
                GValue value = {0};

                gchar *prop = g_match_info_fetch (match, 1);
                gchar *string_value = g_match_info_fetch (match, 2);

                g_value_init (&value, G_TYPE_STRING);
                g_value_set_string (&value, string_value);

                pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (task), prop);

                if (pspec != NULL) {
                    GValue target_value = {0};

                    g_value_init (&target_value, pspec->value_type);
                    g_value_transform (&value, &target_value);
                    g_object_set_property (G_OBJECT (task), prop, &target_value);
                }
                else {
                    g_warning ("`%s' does not have property `%s'", desc->name, prop);
                }

                g_match_info_free (match);
                g_free (prop);
                g_free (string_value);
            }
            else {
                g_warning ("Expected property assignment or `!' but got `%s' instead", prop_assignment);
            }
        }

        if (prev != NULL)
            ufo_task_graph_connect_nodes (graph, prev, task);

        prev = task;
    }

    g_regex_unref (assignment);
    return graph;
}

static void
progress_update (gpointer user)
{
    static int n = 0;

    if ((n++ % 5) == 0)
        g_print (".");
}

static GValueArray *
string_array_to_value_array (gchar **array)
{
    GValueArray *result = NULL;

    if (array == NULL)
        return NULL;

    result = g_value_array_new (0);

    for (guint i = 0; array[i] != NULL; i++) {
        GValue *tmp = (GValue *) g_malloc0 (sizeof (GValue));
        g_value_init (tmp, G_TYPE_STRING);
        g_value_set_string (tmp, array[i]);
        result = g_value_array_append (result, tmp);
    }

    return result;
}

int
main(int argc, char* argv[])
{
    UfoPluginManager *pm;
    UfoTaskGraph *graph;
    UfoBaseScheduler *sched;
    GList *pipeline;
    GOptionContext *context;
    UfoResources *resources = NULL;
    GValueArray *address_list = NULL;
    GError *error = NULL;

    static gboolean progress = FALSE;
    static gboolean trace = FALSE;
    static gboolean do_time = FALSE;
    static gchar **addresses = NULL;
    static gchar *dump = NULL;

    static GOptionEntry entries[] = {
        { "progress", 'p', 0, G_OPTION_ARG_NONE, &progress, "show progress", NULL },
        { "trace", 't', 0, G_OPTION_ARG_NONE, &trace, "enable tracing", NULL },
        { "time", 0, 0, G_OPTION_ARG_NONE, &do_time, "print run time", NULL },
        { "address", 'a', 0, G_OPTION_ARG_STRING_ARRAY, &addresses, "Address of remote server running `ufod'", NULL },
        { "dump", 'd', 0, G_OPTION_ARG_STRING, &dump, "Dump to JSON file", NULL },
        { NULL }
    };

#if !(GLIB_CHECK_VERSION (2, 36, 0))
    g_type_init ();
#endif

    context = g_option_context_new ("TASK [PROP=VAR [PROP=VAR ...]] ! [TASK ...]");
    g_option_context_add_main_entries (context, entries, NULL);

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print ("Error parsing options: %s\n", error->message);
        return 1;
    }

    if (argc == 1) {
        g_print ("%s", g_option_context_get_help (context, TRUE, NULL));
        return 0;
    }

    pipeline = tokenize_args (argc - 1, &argv[1]);
    pm = ufo_plugin_manager_new ();
    graph = parse_pipeline (pipeline, pm, &error);

    if (error != NULL) {
        g_print ("Error parsing pipeline: %s\n", error->message);
        return 1;
    }

    if (progress) {
        UfoTaskNode *leaf;
        GList *leaves;

        leaves = ufo_graph_get_leaves (UFO_GRAPH (graph));
        leaf = UFO_TASK_NODE (leaves->data);
        g_list_free (leaves);

        g_signal_connect (leaf, "processed", G_CALLBACK (progress_update), NULL);
    }

    sched = ufo_scheduler_new ();

    if (trace) {
        g_object_set (sched, "enable-tracing", TRUE, NULL);
    }

    address_list = string_array_to_value_array (addresses);

    if (address_list) {
        resources = UFO_RESOURCES (ufo_resources_new (NULL));
        g_object_set (G_OBJECT (resources), "remotes", address_list, NULL);
        g_value_array_free (address_list);
        ufo_base_scheduler_set_resources (sched, resources);
    }

    ufo_base_scheduler_run (sched, graph, &error);

    if (error != NULL) {
        g_print ("Error executing pipeline: %s\n", error->message);
    }

    if (progress) {
        g_print ("\n");
    }

    if (do_time) {
        gdouble run_time;

        g_object_get (sched, "time", &run_time, NULL);
        g_print ("%3.5fs\n", run_time);
    }

    if (resources) {
        g_object_unref (resources);
    }

    if (dump) {
        ufo_task_graph_save_to_json (graph, dump, &error);

        if (error != NULL)
            g_print ("Error dumping task graph: %s\n", error->message);
    }

    g_object_unref (graph);
    g_object_unref (pm);
    g_strfreev (addresses);

    return 0;
}
