#ifndef __UFO_ELEMENT_H
#define __UFO_ELEMENT_H

#include <glib-object.h>

#include "ufo-filter.h"

#define UFO_TYPE_ELEMENT             (ufo_element_get_type())
#define UFO_ELEMENT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), UFO_TYPE_ELEMENT, UfoElement))
#define UFO_IS_ELEMENT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), UFO_TYPE_ELEMENT))
#define UFO_ELEMENT_GET_INTERFACE(inst)     (G_TYPE_INSTANCE_GET_INTERFACE((inst), UFO_TYPE_ELEMENT, UfoElementInterface))

typedef struct _UfoElement          UfoElement;
typedef struct _UfoElementInterface UfoElementInterface;  

/**
 * \class UfoElement
 *
 * An UfoElement is a base-class for either a computation leaf or some container
 * holding references to more UfoElements.
 *
 * <b>Signals</b>
 *      - <tt>"finished"</tt> :  Indicates that an instance is not going to produce more
 *          data
 *
 * <b>Properties</b>
 *      - <tt>"name"</tt> : Name of the associated filter
 */
struct _UfoElementInterface {
    GTypeInterface parent;

    /* default signal receiver */
    void (*finished) (UfoElement *element);

    /* methods */
    void (*process) (UfoElement *element);
    void (*print) (UfoElement *element);
    void (*set_input_queue) (UfoElement *element, GAsyncQueue *queue);
    void (*set_output_queue) (UfoElement *element, GAsyncQueue *queue);
    GAsyncQueue *(*get_input_queue) (UfoElement *element);
    GAsyncQueue *(*get_output_queue) (UfoElement *element);
};

void ufo_element_process(UfoElement *element);
void ufo_element_print(UfoElement *element);

void ufo_element_set_input_queue(UfoElement *element, GAsyncQueue *queue);
void ufo_element_set_output_queue(UfoElement *element, GAsyncQueue *queue);
GAsyncQueue *ufo_element_get_input_queue(UfoElement *element);
GAsyncQueue *ufo_element_get_output_queue(UfoElement *element);

GType ufo_element_get_type(void);

#endif
