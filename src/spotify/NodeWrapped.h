#ifndef _NODE_WRAPPED_H
#define _NODE_WRAPPED_H

#include <string>
#include <map>
#include <iostream>

#include <uv.h>
#include <node.h>

#include "../NodeCallback.h"
#include "V8Wrapped.h"
#include "../SpotifyService/SpotifyService.h"

/*
 * The spotify service saves the async handle for communication with the nodeJS thread.
 * As most of the NodeWrapped objects will play a role in the libspotify thread this handle
 * is needed here, so we have a reference to the spotify service.
 */
extern SpotifyService* spotifyService;

/**
 * A class used as a base class for wrapping objects to node objects
 * 
 * Provides callback handling, waiting methods via pthread conditions 
 **/
template <class T>
class NodeWrapped : public node::ObjectWrap, V8Wrapped {
  template <class S> friend class StaticCallbackSetter;
  //friend class Callback<T>;
  typedef void (T::*SimpleMethod)();

  public:
    NodeWrapped() : asyncHandle(&spotifyService->callNodeThread), doneCondition(0) {
      pthread_mutex_init(&waitingMutex, NULL);
      pthread_mutex_init(&lockingMutex, NULL);
      pthread_cond_init(&waitingCondition, NULL);
    }; 

    ~NodeWrapped() {
      //TODO: unwrap, delete persistent handle
      std::cout << "Destructor called! Please check!" << std::endl;
    };

    /**
     * Get a V8 handle with the Javascript object inside.
     **/
    v8::Handle<v8::Object> getV8Object() {
      //We cannot open a new HandleScope here, as this gets called in the spotify thread!
      //check if the handle from ObjectWrap has been initialized and if not wrap the object in a new JS instance
      if(handle_.IsEmpty()) {
        v8::Persistent<v8::Object> o = v8::Persistent<v8::Object>::New(constructor->NewInstance());
        this->Wrap(o);
      }
      return handle_;
    }

    /**
     * Save a Javascript callback under a certain name.
     **/
    static v8::Handle<v8::Value> on(const v8::Arguments& args) {
      v8::HandleScope scope;
      T* object = node::ObjectWrap::Unwrap<T>(args.This());
      v8::String::Utf8Value callbackName(args[0]->ToString());
      v8::Handle<v8::Function> fun = v8::Handle<v8::Function>::Cast(args[1]);
      object->callbacks[*callbackName] = v8::Persistent<v8::Function>::New(fun);
      return scope.Close(v8::Undefined());
    }

    /**
     * Deletes all callbacks that are saved under a name.
     **/
    static v8::Handle<v8::Value> off(const v8::Arguments& args) {
      v8::HandleScope scope;
      T* object = node::ObjectWrap::Unwrap<T>(args.This());
      v8::String::Utf8Value callbackName(args[0]->ToString());
      int deleted = object->callbacks.erase(*callbackName);
      return scope.Close(v8::Integer::New(deleted));
    }

    /**
     * Call a Javascript callback by name. The callback will be executed in the nodeJS thread.
     * First, object wide callbacks will be searched, then, class wide callbacks.
     * If no callback is found, nothing happens.
     **/
    void call(std::string name)  {
      std::map< std::string, v8::Persistent<v8::Function> >::iterator it;
      it = callbacks.find(name);

      v8::Persistent<v8::Function>* fun = 0;

      //Check if a callback for the given name was found in this object
      if(it != callbacks.end()) {
        //Get the adress of the callback function and send it to the node thread
        //This needs to be the adress from the map element, otherwise we would pass the adress of a local and it fails on the node side.
        fun = &it->second;
      } else {
        //search static callbacks
        it = staticCallbacks.find(name);
        if(it != staticCallbacks.end()) {
          fun = &it->second;
        }
      }
      
      if(fun != 0) {
        //Trigger the nodeJS eventloop
        NodeCallback* nodeCallback = new NodeCallback();
        nodeCallback->object = this;
        nodeCallback->function = fun;
        asyncHandle->data  = (void*)nodeCallback;
        uv_async_send(asyncHandle);
      }
    };
  protected:
    static void emptySetter(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::AccessorInfo& info) {};
    uv_async_t* asyncHandle;
    static v8::Persistent<v8::Function> constructor;
      
    /**
     * A method to wait for another method in this object to complete, e.g. if it is called in another thread
     * This method locks an object wide mutex, so it can only be called one at a time.
    **/
    void wait() {
      pthread_mutex_lock(&waitingMutex);
      while(!doneCondition) {
        pthread_cond_wait(&waitingCondition, &waitingMutex);
      }
      doneCondition = 0;
      pthread_mutex_unlock(&waitingMutex);
    };
      
    /**
     * Signal the SpotifyWrapped::wait() method that some action is complete.
    **/
    void done() {
      pthread_mutex_lock(&waitingMutex);
      doneCondition = 1;
      pthread_cond_signal(&waitingCondition);
      pthread_mutex_unlock(&waitingMutex);
    };
    
    pthread_mutex_t lockingMutex;

    /**
     * Basic init method for a wrapped node object. Provides a callback setter "on" and sets the classname.
     */
    static v8::Handle<v8::FunctionTemplate> init(const char* className) {
      v8::Local<v8::FunctionTemplate> constructorTemplate = v8::FunctionTemplate::New();
      constructorTemplate->SetClassName(v8::String::NewSymbol(className));
      constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);
      NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "on", on);
      NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "off", off);
      return constructorTemplate;
    }
  private:
    int doneCondition;
    pthread_mutex_t waitingMutex;
    pthread_cond_t waitingCondition;
    std::map<std::string, v8::Persistent<v8::Function> > callbacks;
    static std::map<std::string, v8::Persistent<v8::Function> > staticCallbacks;
};

template <class T> std::map<std::string, v8::Persistent<v8::Function> > NodeWrapped<T>::staticCallbacks;
template <class T> v8::Persistent<v8::Function> NodeWrapped<T>::constructor;
#endif
