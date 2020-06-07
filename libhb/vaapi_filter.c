/* ********************************************************************* *\

Copyright (C) 2013 Intel Corporation.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.
- Neither the name of Intel Corporation nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL INTEL CORPORATION BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

\* ********************************************************************* */

#include "handbrake/project.h"

#if HB_PROJECT_FEATURE_VAAPI

#include "handbrake/handbrake.h"
#include "handbrake/hbavfilter.h"
#include "handbrake/hbffmpeg.h"

struct hb_filter_private_s
{
    hb_job_t                     * job;
    hb_buffer_list_t               list;
    hb_avfilter_graph_t          * graph;
};

static int hb_vaapi_filter_init( hb_filter_object_t * filter,
                                 hb_filter_init_t * init );

static int hb_vaapi_filter_work( hb_filter_object_t * filter,
                                 hb_buffer_t ** buf_in,
                                 hb_buffer_t ** buf_out );

static hb_filter_info_t * hb_vaapi_filter_info( hb_filter_object_t * filter );

static void hb_vaapi_filter_close( hb_filter_object_t * filter );

hb_filter_object_t hb_filter_vaapi_hwupload =
{
    .id                = HB_FILTER_VAAPI_HWUPLOAD,
    .enforce_order     = 1,
    .name              = "VAAPI hardware upload",
    .settings          = NULL,
    .init              = hb_vaapi_filter_init,
    .work              = hb_vaapi_filter_work,
    .close             = hb_vaapi_filter_close,
    .info              = hb_vaapi_filter_info,
};

static int hb_vaapi_filter_init( hb_filter_object_t * filter,
                                 hb_filter_init_t * init )
{
    hb_dict_t        * settings;
    hb_filter_init_t   filter_init;
    hb_value_array_t * filters;

    filter->private_data = calloc( 1, sizeof(struct hb_filter_private_s) );
    hb_filter_private_t * pv = filter->private_data;

    filters = hb_value_array_init();

    settings = hb_dict_init();
    hb_dict_set(settings, "pix_fmts", hb_value_string("vaapi"));
    hb_avfilter_append_dict(filters, "format", settings);

    settings = hb_dict_init();
    hb_avfilter_append_dict(filters, "hwupload", settings);

    filter_init.pix_fmt           = AV_PIX_FMT_VAAPI;
    filter_init.geometry.width    = init->geometry.width;
    filter_init.geometry.height   = init->geometry.height;
    filter_init.geometry.par.num  = init->geometry.par.num;
    filter_init.geometry.par.den  = init->geometry.par.den;
    filter_init.time_base.num     = init->time_base.num;
    filter_init.time_base.den     = init->time_base.den;
    filter_init.vrate.num         = init->vrate.num;
    filter_init.vrate.den         = init->vrate.den;

    pv->graph = hb_avfilter_graph_init(filters, &filter_init);
    hb_value_free(&filters);
    if (!pv->graph)
    {
        hb_error("hb_vaapi_filter_init: failed to create filter graph");
        goto fail;
    }

    hb_buffer_list_clear(&pv->list);
    pv->job = init->job;

    hb_avfilter_graph_update_init(pv->graph, init);

    return 0;

fail:
    free(pv);

    return 1;
}

static hb_filter_info_t * hb_vaapi_filter_info( hb_filter_object_t * filter )
{
    hb_filter_private_t *pv = filter->private_data;
    hb_filter_info_t    * info;

    if( !pv )
        return NULL;

    info = calloc(1, sizeof(hb_filter_info_t));
    info->human_readable_desc = malloc(128);
    info->human_readable_desc[0] = 0;

    snprintf(info->human_readable_desc, 128, "vaapi hardware upload");

    return info;
}

static void hb_vaapi_filter_close( hb_filter_object_t * filter )
{
    hb_filter_private_t * pv = filter->private_data;

    if ( !pv )
    {
        return;
    }

    hb_avfilter_graph_close(&pv->graph);
    hb_buffer_list_close(&pv->list);
    free( pv );
    filter->private_data = NULL;
}

static hb_buffer_t* filterFrame( hb_filter_private_t * pv, hb_buffer_t * in )
{
    hb_buffer_list_t   list;
    hb_buffer_t      * buf, * next;

    hb_avfilter_add_buf(pv->graph, in);
    buf = hb_avfilter_get_buf(pv->graph);
    while (buf != NULL)
    {
        hb_buffer_list_append(&pv->list, buf);
        buf = hb_avfilter_get_buf(pv->graph);
    }

    // Delay one frame so we can set the stop time of the output buffer
    hb_buffer_list_clear(&list);
    while (hb_buffer_list_count(&pv->list) > 1)
    {
        buf  = hb_buffer_list_rem_head(&pv->list);
        next = hb_buffer_list_head(&pv->list);

        buf->s.stop = next->s.start;
        hb_buffer_list_append(&list, buf);
    }

    return hb_buffer_list_head(&list);
}

static int hb_vaapi_filter_work( hb_filter_object_t * filter,
                                 hb_buffer_t ** buf_in,
                                 hb_buffer_t ** buf_out )
{
    hb_filter_private_t * pv = filter->private_data;
    hb_buffer_t * in = *buf_in;

    if (in->s.flags & HB_BUF_FLAG_EOF)
    {
        hb_buffer_t * out  = filterFrame(pv, NULL);
        hb_buffer_t * last = hb_buffer_list_tail(&pv->list);
        if (last != NULL && last->s.start != AV_NOPTS_VALUE)
        {
            last->s.stop = last->s.start + last->s.duration;
        }
        hb_buffer_list_prepend(&pv->list, out);
        hb_buffer_list_append(&pv->list, in);
        *buf_out = hb_buffer_list_clear(&pv->list);
        *buf_in = NULL;
        return HB_FILTER_DONE;
    }

    *buf_out = filterFrame(pv, in);

    return HB_FILTER_OK;
}

#endif // HB_PROJECT_FEATURE_VAAPI

