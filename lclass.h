/**
 * @file lclass.h
 * @brief Lua object-oriented system - Class and object support.
 * Provides native class definition, inheritance, and instantiation.
 */

#ifndef lclass_h
#define lclass_h

#include "lobject.h"
#include "lstate.h"
#include "ltable.h"

/*
** =====================================================================
** Core structures
** =====================================================================
*/

/** @name Class Flags */
/**@{*/
#define CLASS_FLAG_FINAL      (1 << 0)   /**< Class cannot be inherited. */
#define CLASS_FLAG_ABSTRACT   (1 << 1)   /**< Abstract class, cannot be instantiated. */
#define CLASS_FLAG_INTERFACE  (1 << 2)   /**< Interface type. */
#define CLASS_FLAG_SEALED     (1 << 3)   /**< Sealed class. */
/**@}*/

/** @name Access Control Levels */
/**@{*/
#define ACCESS_PUBLIC     0   /**< Public member. */
#define ACCESS_PROTECTED  1   /**< Protected member (accessible by subclasses). */
#define ACCESS_PRIVATE    2   /**< Private member (accessible only by this class). */
/**@}*/

/** @name Member Type Flags */
/**@{*/
#define MEMBER_METHOD     (1 << 0)   /**< Method. */
#define MEMBER_FIELD      (1 << 1)   /**< Field. */
#define MEMBER_STATIC     (1 << 2)   /**< Static member. */
#define MEMBER_CONST      (1 << 3)   /**< Constant member. */
#define MEMBER_VIRTUAL    (1 << 4)   /**< Virtual method (can be overridden). */
#define MEMBER_OVERRIDE   (1 << 5)   /**< Overrides parent method. */
#define MEMBER_ABSTRACT   (1 << 6)   /**< Abstract method (must be implemented by subclass). */
#define MEMBER_FINAL      (1 << 7)   /**< Final method (cannot be overridden). */
/**@}*/

/** @name Class Metadata Keys */
/**@{*/
#define CLASS_KEY_NAME       "__classname"    /**< Class name. */
#define CLASS_KEY_PARENT     "__parent"       /**< Parent class reference. */
#define CLASS_KEY_METHODS    "__methods"      /**< Public methods table. */
#define CLASS_KEY_STATICS    "__statics"      /**< Static members table. */
#define CLASS_KEY_PRIVATES   "__privates"     /**< Private members table. */
#define CLASS_KEY_PROTECTED  "__protected"    /**< Protected members table. */
#define CLASS_KEY_INIT       "__init__"       /**< Constructor. */
#define CLASS_KEY_DESTRUCTOR "__gc"           /**< Destructor. */
#define CLASS_KEY_ISCLASS    "__isclass"      /**< Is-a-class flag. */
#define CLASS_KEY_INTERFACES "__interfaces"   /**< Implemented interfaces list. */
#define CLASS_KEY_FLAGS      "__flags"        /**< Class flags. */
#define CLASS_KEY_ABSTRACTS  "__abstracts"    /**< Abstract methods table. */
#define CLASS_KEY_FINALS     "__finals"       /**< Final methods table. */
#define CLASS_KEY_GETTERS    "__getters"      /**< Public getter table. */
#define CLASS_KEY_SETTERS    "__setters"      /**< Public setter table. */
#define CLASS_KEY_PRIVATE_GETTERS   "__private_getters"   /**< Private getter table. */
#define CLASS_KEY_PRIVATE_SETTERS   "__private_setters"   /**< Private setter table. */
#define CLASS_KEY_PROTECTED_GETTERS "__protected_getters" /**< Protected getter table. */
#define CLASS_KEY_PROTECTED_SETTERS "__protected_setters" /**< Protected setter table. */
#define CLASS_KEY_MEMBER_FLAGS "__member_flags" /**< Member flags table. */
/**@}*/

/** @name Object Metadata Keys */
/**@{*/
#define OBJ_KEY_CLASS        "__class"        /**< Class the object belongs to. */
#define OBJ_KEY_ISOBJ        "__isobject"     /**< Is-an-object flag. */
#define OBJ_KEY_PRIVATES     "__obj_privates" /**< Object private data. */
/**@}*/

/*
** =====================================================================
** Core Functions
** =====================================================================
*/

/**
 * @brief Creates a new class.
 * @param L Lua state.
 * @param name Class name.
 */
LUAI_FUNC void luaC_newclass(lua_State *L, TString *name);

/**
 * @brief Sets inheritance relationship.
 * @param L Lua state.
 * @param child_idx Stack index of the subclass.
 * @param parent_idx Stack index of the parent class.
 */
LUAI_FUNC void luaC_inherit(lua_State *L, int child_idx, int parent_idx);

/**
 * @brief Creates an instance of a class.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param nargs Number of constructor arguments.
 */
LUAI_FUNC void luaC_newobject(lua_State *L, int class_idx, int nargs);

/**
 * @brief Calls a parent class method.
 * @param L Lua state.
 * @param obj_idx Stack index of the object.
 * @param method Method name.
 */
LUAI_FUNC void luaC_super(lua_State *L, int obj_idx, TString *method);

/**
 * @brief Sets a class method.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param name Method name.
 * @param func_idx Stack index of the function.
 */
LUAI_FUNC void luaC_setmethod(lua_State *L, int class_idx, TString *name, 
                               int func_idx);

/**
 * @brief Sets a static member of a class.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param name Member name.
 * @param value_idx Stack index of the value.
 */
LUAI_FUNC void luaC_setstatic(lua_State *L, int class_idx, TString *name,
                               int value_idx);

/**
 * @brief Gets a property from an object (considering inheritance).
 * @param L Lua state.
 * @param obj_idx Stack index of the object.
 * @param key Property name.
 */
LUAI_FUNC void luaC_getprop(lua_State *L, int obj_idx, TString *key);

/**
 * @brief Sets a property on an object.
 * @param L Lua state.
 * @param obj_idx Stack index of the object.
 * @param key Property name.
 * @param value_idx Stack index of the value.
 */
LUAI_FUNC void luaC_setprop(lua_State *L, int obj_idx, TString *key,
                             int value_idx);

/**
 * @brief Checks if an object is an instance of a class.
 * @param L Lua state.
 * @param obj_idx Stack index of the object.
 * @param class_idx Stack index of the class.
 * @return 1 if instance, 0 otherwise.
 */
LUAI_FUNC int luaC_instanceof(lua_State *L, int obj_idx, int class_idx);

/**
 * @brief Checks if a value is a class.
 * @param L Lua state.
 * @param idx Stack index.
 * @return 1 if class, 0 otherwise.
 */
LUAI_FUNC int luaC_isclass(lua_State *L, int idx);

/**
 * @brief Checks if a value is an object instance.
 * @param L Lua state.
 * @param idx Stack index.
 * @return 1 if object, 0 otherwise.
 */
LUAI_FUNC int luaC_isobject(lua_State *L, int idx);

/**
 * @brief Gets the class of an object.
 * @param L Lua state.
 * @param obj_idx Stack index of the object.
 */
LUAI_FUNC void luaC_getclass(lua_State *L, int obj_idx);

/**
 * @brief Gets the parent class of a class.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 */
LUAI_FUNC void luaC_getparent(lua_State *L, int class_idx);

/**
 * @brief Gets the name of a class.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @return Class name string.
 */
LUAI_FUNC const char *luaC_classname(lua_State *L, int class_idx);

/**
 * @brief Creates a new interface.
 * @param L Lua state.
 * @param name Interface name.
 */
LUAI_FUNC void luaC_newinterface(lua_State *L, TString *name);

/**
 * @brief Implements an interface in a class.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param interface_idx Stack index of the interface.
 */
LUAI_FUNC void luaC_implement(lua_State *L, int class_idx, int interface_idx);

/**
 * @brief Checks if a class implements an interface.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param interface_idx Stack index of the interface.
 * @return 1 if implements, 0 otherwise.
 */
LUAI_FUNC int luaC_implements(lua_State *L, int class_idx, int interface_idx);

/**
 * @brief Initializes the class system.
 * @param L Lua state.
 */
LUAI_FUNC void luaC_initclass(lua_State *L);

/**
 * @brief Sets a private member.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param name Member name.
 * @param value_idx Stack index of the value.
 */
LUAI_FUNC void luaC_setprivate(lua_State *L, int class_idx, TString *name,
                                int value_idx);

/**
 * @brief Sets a protected member.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param name Member name.
 * @param value_idx Stack index of the value.
 */
LUAI_FUNC void luaC_setprotected(lua_State *L, int class_idx, TString *name,
                                  int value_idx);

/**
 * @brief Checks member access permissions.
 * @param L Lua state.
 * @param obj_idx Stack index of the object.
 * @param key Member name.
 * @param caller_class_idx Class index of the caller (0 for external).
 * @return ACCESS_PUBLIC, ACCESS_PROTECTED, ACCESS_PRIVATE, or -1.
 */
LUAI_FUNC int luaC_checkaccess(lua_State *L, int obj_idx, TString *key,
                                int caller_class_idx);

/**
 * @brief Checks if a class is a subclass of another.
 * @param L Lua state.
 * @param child_idx Stack index of possible subclass.
 * @param parent_idx Stack index of possible parent class.
 * @return 1 if subclass, 0 otherwise.
 */
LUAI_FUNC int luaC_issubclass(lua_State *L, int child_idx, int parent_idx);

/**
 * @brief Declares an abstract method.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param name Method name.
 * @param nparams Expected number of parameters.
 */
LUAI_FUNC void luaC_setabstract(lua_State *L, int class_idx, TString *name, int nparams);

/**
 * @brief Sets a final method.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param name Method name.
 * @param func_idx Stack index of the function.
 */
LUAI_FUNC void luaC_setfinal(lua_State *L, int class_idx, TString *name, 
                              int func_idx);

/**
 * @brief Verifies all abstract methods are implemented.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @return 1 on success, 0 on failure.
 */
LUAI_FUNC int luaC_verify_abstracts(lua_State *L, int class_idx);

/**
 * @brief Verifies all interface methods are correctly implemented.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @return 1 on success, 0 on failure.
 */
LUAI_FUNC int luaC_verify_interfaces(lua_State *L, int class_idx);

/**
 * @brief Checks if a method can be overridden.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param name Method name.
 * @return 1 if can override, 0 otherwise.
 */
LUAI_FUNC int luaC_can_override(lua_State *L, int class_idx, TString *name);

/**
 * @brief Sets a getter method.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param prop_name Property name.
 * @param func_idx Stack index of getter function.
 * @param access_level Access level.
 */
LUAI_FUNC void luaC_setgetter(lua_State *L, int class_idx, TString *prop_name,
                               int func_idx, int access_level);

/**
 * @brief Sets a setter method.
 * @param L Lua state.
 * @param class_idx Stack index of the class.
 * @param prop_name Property name.
 * @param func_idx Stack index of setter function.
 * @param access_level Access level.
 */
LUAI_FUNC void luaC_setsetter(lua_State *L, int class_idx, TString *prop_name,
                               int func_idx, int access_level);

/**
 * @brief Sets member flags.
 */
LUAI_FUNC void luaC_setmemberflags(lua_State *L, int class_idx, TString *name,
                                    int flags);

/**
 * @brief Gets member flags.
 */
LUAI_FUNC int luaC_getmemberflags(lua_State *L, int class_idx, TString *name);

#endif
