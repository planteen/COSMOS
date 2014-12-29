/*
# Copyright 2014 Ball Aerospace & Technologies Corp.
# All Rights Reserved.
#
# This program is free software; you can modify and/or redistribute it
# under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation; version 3 with
# attribution addendums as found in the LICENSE.txt
*/

#include "ruby.h"
#include "stdio.h"

static const int endianness_check = 1;
static VALUE HOST_ENDIANNESS = Qnil;
static VALUE ZERO_STRING = Qnil;
static VALUE ASCII_8BIT_STRING = Qnil;

static VALUE mCosmos = Qnil;
static VALUE cBinaryAccessor = Qnil;
static VALUE cStructure = Qnil;
static VALUE cStructureItem = Qnil;

static ID id_method_to_s = 0;
static ID id_method_raise_buffer_error = 0;
static ID id_method_read_array = 0;
static ID id_method_force_encoding = 0;
static ID id_method_freeze = 0;

static ID id_ivar_buffer = 0;
static ID id_ivar_bit_offset = 0;
static ID id_ivar_bit_size = 0;
static ID id_ivar_array_size = 0;
static ID id_ivar_endianness = 0;
static ID id_ivar_data_type = 0;
static ID id_ivar_default_endianness = 0;
static ID id_ivar_item_class = 0;
static ID id_ivar_items = 0;
static ID id_ivar_sorted_items = 0;
static ID id_ivar_defined_length = 0;
static ID id_ivar_defined_length_bits = 0;
static ID id_ivar_pos_bit_size = 0;
static ID id_ivar_neg_bit_size = 0;
static ID id_ivar_fixed_size = 0;
static ID id_ivar_short_buffer_allowed = 0;
static ID id_ivar_mutex = 0;

static ID id_const_ZERO_STRING = 0;

static VALUE symbol_LITTLE_ENDIAN = Qnil;
static VALUE symbol_BIG_ENDIAN = Qnil;
static VALUE symbol_INT = Qnil;
static VALUE symbol_UINT = Qnil;
static VALUE symbol_FLOAT = Qnil;
static VALUE symbol_STRING = Qnil;
static VALUE symbol_BLOCK = Qnil;
static VALUE symbol_DERIVED = Qnil;
static VALUE symbol_read = Qnil;

/*
 * Perform an left bit shift on a string
 */
static void left_shift_byte_array (unsigned char* array, int array_length, int shift)
{
  int current_index = 0;
  int previous_index = 0;
  unsigned char saved_bits = 0;
  unsigned char saved_bits_mask = (0xFF << (8 - shift));
  unsigned char sign_extension_remove_mask = ~(0xFF << shift);

  for (current_index = 0; current_index < array_length; current_index++)
  {
    /* Save bits that will be lost */
    saved_bits = ((array[current_index] & saved_bits_mask) >> (8 - shift)) & sign_extension_remove_mask;

    /* Perform shift on current byte */
    array[current_index] <<= shift;

    /* Add Saved bits to end of previous byte */
    if (current_index > 0)
    {
      array[previous_index] |= saved_bits;
    }

    /* Update previous index */
    previous_index = current_index;
  }
}

/*
 * Perform an unsigned right bit shift on a string
 */
static void unsigned_right_shift_byte_array (unsigned char* array, int array_length, int shift)
{
  int current_index = 0;
  int previous_index = 0;
  unsigned char saved_bits = 0;
  unsigned char saved_bits_mask = ~(0xFF << shift);
  unsigned char sign_extension_remove_mask = ~(0xFF << (8 - shift));

  for (current_index = array_length - 1; current_index >= 0; current_index--)
  {
    /* Save bits that will be lost */
    saved_bits = (array[current_index] & saved_bits_mask) << (8 - shift);

    /* Perform shift on current byte */
    array[current_index] = (array[current_index] >> shift) & sign_extension_remove_mask;

    /* Add Saved bits to beginning of previous byte */
    if (current_index != (array_length - 1))
    {
      array[previous_index] |= saved_bits;
    }

    /* Update previous index */
    previous_index = current_index;
  }
}

/*
 * Perform an signed right bit shift on a string
 */
static void signed_right_shift_byte_array (unsigned char* array, int array_length, int shift)
{
  unsigned char start_bits_mask = (0xFF << (8 - shift));
  int is_signed = (0x80 & array[0]);

  unsigned_right_shift_byte_array(array, array_length, shift);

  if (is_signed)
  {
    array[0] |= start_bits_mask;
  }
}

/*
 * Perform an unsigned bit shift on a string
 */
static void unsigned_shift_byte_array (unsigned char* array, int array_length, int shift)
{
  if (shift < 0)
  {
    left_shift_byte_array(array, array_length, -shift);
  }
  else if (shift > 0)
  {
    unsigned_right_shift_byte_array(array, array_length, shift);
  }
}

/*
 * Reverse the byte order in a string.
 */
static void reverse_bytes (unsigned char* array, int array_length)
{
  int first_index  = 0;
  int second_index = 0;
  unsigned char temp_byte = 0;

  for (first_index = 0; first_index < (array_length / 2); first_index++)
  {
    second_index = array_length - 1 - first_index;
    temp_byte = array[first_index];
    array[first_index] = array[second_index];
    array[second_index] = temp_byte;
  }
}

static void read_aligned_16(int lower_bound, int upper_bound, VALUE endianness, unsigned char *buffer, unsigned char *read_value) {
  if (endianness == HOST_ENDIANNESS)
  {
    read_value[1] = buffer[upper_bound];
    read_value[0] = buffer[lower_bound];
  }
  else
  {
    read_value[0] = buffer[upper_bound];
    read_value[1] = buffer[lower_bound];
  }
}

static void read_aligned_32(int lower_bound, int upper_bound, VALUE endianness, unsigned char *buffer, unsigned char *read_value) {
  if (endianness == HOST_ENDIANNESS)
  {
    read_value[3] = buffer[upper_bound];
    read_value[2] = buffer[upper_bound - 1];
    read_value[1] = buffer[lower_bound + 1];
    read_value[0] = buffer[lower_bound];
  }
  else
  {
    read_value[0] = buffer[upper_bound];
    read_value[1] = buffer[upper_bound - 1];
    read_value[2] = buffer[lower_bound + 1];
    read_value[3] = buffer[lower_bound];
  }
}

static void read_aligned_64(int lower_bound, int upper_bound, VALUE endianness, unsigned char *buffer, unsigned char *read_value) {
  if (endianness == HOST_ENDIANNESS)
  {
    read_value[7] = buffer[upper_bound];
    read_value[6] = buffer[upper_bound - 1];
    read_value[5] = buffer[upper_bound - 2];
    read_value[4] = buffer[upper_bound - 3];
    read_value[3] = buffer[lower_bound + 3];
    read_value[2] = buffer[lower_bound + 2];
    read_value[1] = buffer[lower_bound + 1];
    read_value[0] = buffer[lower_bound];
  }
  else
  {
    read_value[0] = buffer[upper_bound];
    read_value[1] = buffer[upper_bound - 1];
    read_value[2] = buffer[upper_bound - 2];
    read_value[3] = buffer[upper_bound - 3];
    read_value[4] = buffer[lower_bound + 3];
    read_value[5] = buffer[lower_bound + 2];
    read_value[6] = buffer[lower_bound + 1];
    read_value[7] = buffer[lower_bound];
  }
}

static void read_bitfield(int lower_bound, int upper_bound, int bit_offset, int bit_size, int given_bit_offset, int given_bit_size, VALUE endianness, unsigned char* buffer, int buffer_length, unsigned char* read_value) {
  /* Local variables */
  int num_bytes = 0;
  int total_bits = 0;
  int start_bits = 0;
  int end_bits = 0;
  int temp_upper = 0;
  unsigned char end_mask = 0;

  /* Copy Data For Bitfield into read_value */
  if (endianness == symbol_LITTLE_ENDIAN)
  {
    /* Bitoffset always refers to the most significant bit of a bitfield */
    num_bytes = (((bit_offset % 8) + bit_size - 1) / 8) + 1;
    upper_bound = bit_offset / 8;
    lower_bound = upper_bound - num_bytes + 1;

    if (lower_bound < 0) {
      rb_raise(rb_eArgError, "LITTLE_ENDIAN bitfield with bit_offset %d and bit_size %d is invalid", given_bit_offset, given_bit_size);
    }

    memcpy(read_value, &buffer[lower_bound], num_bytes);
    reverse_bytes(read_value, num_bytes);
  }
  else
  {
    num_bytes = upper_bound - lower_bound + 1;
    memcpy(read_value, &buffer[lower_bound], num_bytes);
  }

  /* Determine temp upper bound */
  temp_upper = upper_bound - lower_bound;

  /* Handle Bitfield */
  total_bits = (temp_upper + 1) * 8;
  start_bits = bit_offset % 8;
  end_bits = total_bits - start_bits - bit_size;
  end_mask = 0xFF << end_bits;

  /* Mask off unwanted bits at end */
  read_value[temp_upper] &= end_mask;

  /* Shift off unwanted bits at beginning */
  unsigned_shift_byte_array(read_value, num_bytes, -start_bits);
}

/*
 * Reads binary data of any data type from a buffer
 *
 * @param bit_offset [Integer] Bit offset to the start of the item. A
 *   negative number means to offset from the end of the buffer.
 * @param bit_size [Integer] Size of the item in bits
 * @param data_type [Symbol] {DATA_TYPES}
 * @param buffer [String] Binary string buffer to read from
 * @param endianness [Symbol] {ENDIANNESS}
 * @return [Integer] value read from the buffer
 */
static VALUE binary_accessor_read(VALUE self, VALUE param_bit_offset, VALUE param_bit_size, VALUE param_data_type, VALUE param_buffer, VALUE param_endianness)
{
  /* Convert Parameters to C Data Types */
  int bit_offset = FIX2INT(param_bit_offset);
  int bit_size = FIX2INT(param_bit_size);

  /* Local Variables */
  int given_bit_offset = bit_offset;
  int given_bit_size = bit_size;
  signed char signed_char_value = 0;
  unsigned char unsigned_char_value = 0;
  signed short signed_short_value = 0;
  unsigned short unsigned_short_value = 0;
  signed int signed_int_value = 0;
  signed long signed_long_value = 0;
  unsigned int unsigned_int_value = 0;
  signed long long signed_long_long_value = 0;
  unsigned long long unsigned_long_long_value = 0;
  unsigned char* unsigned_char_array = NULL;
  int array_length = 0;
  char* string = NULL;
  int string_length = 0;
  float float_value = 0.0;
  double double_value = 0.0;
  int shift_needed = 0;
  int shift_count = 0;
  int index = 0;
  int num_bits = 0;
  int num_bytes = 0;
  int num_words = 0;
  int upper_bound = 0;
  int lower_bound = 0;
  int byte_aligned = 0;
  VALUE temp_value = Qnil;
  VALUE return_value = Qnil;

  unsigned char* buffer = NULL;
  long buffer_length = 0;

  Check_Type(param_buffer, T_STRING);
  buffer = (unsigned char*) RSTRING_PTR(param_buffer);
  buffer_length = RSTRING_LEN(param_buffer);

  /* Handle negative bit offsets */
  if (bit_offset < 0) {
    if (given_bit_size <= 0) {
      rb_raise(rb_eArgError, "negative or zero bit_sizes (%d) cannot be given with negative bit_offsets (%d)", given_bit_size, given_bit_offset);
    } else {
      bit_offset = (((int)buffer_length * 8) + bit_offset);
      if (bit_offset < 0) {
        rb_funcall(self, id_method_raise_buffer_error, 5, symbol_read, param_buffer, param_data_type, param_bit_offset, param_bit_size);
      }
    }
  }

  /* Handle negative and zero bit sizes */
  if (bit_size <= 0) {
    if ((param_data_type == symbol_STRING) || (param_data_type == symbol_BLOCK)) {
      bit_size = (((int)buffer_length * 8) - bit_offset + bit_size);
      if (bit_size == 0) {
        return rb_str_new2("");
      } else if (bit_size < 0) {
        rb_funcall(self, id_method_raise_buffer_error, 5, symbol_read, param_buffer, param_data_type, param_bit_offset, param_bit_size);
      }
    } else {
      rb_raise(rb_eArgError, "bit_size %d must be positive for data types other than :STRING and :BLOCK", given_bit_size);
    }
  }

  /* define bounds of string to access this item */
  lower_bound = (bit_offset / 8);
  upper_bound = ((bit_offset + bit_size - 1) / 8);

  /* Check for byte alignment */
  byte_aligned = ((bit_offset % 8) == 0);

  /* Sanity check buffer size */
  if (upper_bound >= buffer_length) {
    /* Check special case of little endian bit field */
    if ((param_endianness == symbol_LITTLE_ENDIAN) && ((param_data_type == symbol_INT) || (param_data_type == symbol_UINT)) && (!((byte_aligned) && ((bit_size == 8) || (bit_size == 16) || (bit_size == 32) || (bit_size == 64)))) && (lower_bound < buffer_length)) {
      /* Ok little endian bit field */
    } else {
      rb_funcall(self, id_method_raise_buffer_error, 5, symbol_read, param_buffer, param_data_type, param_bit_offset, param_bit_size);
    }
  }

  if ((param_data_type == symbol_STRING) || (param_data_type == symbol_BLOCK)) {

    /*#######################################
       *# Handle :STRING and :BLOCK data types
       *#######################################*/

    if (byte_aligned) {
      string_length = upper_bound - lower_bound + 1;
      string = malloc(string_length + 1);
      memcpy(string, buffer + lower_bound, string_length);
      string[string_length] = 0;
      if (param_data_type == symbol_STRING) {
        return_value = rb_str_new2(string);
      } else /* param_data_type == symbol_BLOCK */ {
        return_value = rb_str_new(string, string_length);
      }
      free(string);
    } else {
      rb_raise(rb_eArgError, "bit_offset %d is not byte aligned for data_type %s", given_bit_offset, RSTRING_PTR(rb_funcall(param_data_type, id_method_to_s, 0)));
    }

  } else if (param_data_type == symbol_INT) {

    /*###################################
       *# Handle :INT data type
       *###################################*/

    if ((byte_aligned) && ((bit_size == 8) || (bit_size == 16) || (bit_size == 32) || (bit_size == 64))) {
      /*###########################################################
         *# Handle byte-aligned 8, 16, 32, and 64 bit :INT
         *###########################################################*/

      if (bit_size == 8)
      {
        signed_char_value = *((signed char*) &buffer[lower_bound]);
        return_value = INT2FIX(signed_char_value);
      }
      else if (bit_size == 16)
      {
        read_aligned_16(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &signed_short_value);
        return_value = INT2FIX(signed_short_value);
      }
      else if (bit_size == 32)
      {
        read_aligned_32(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &signed_int_value);
        return_value = INT2NUM(signed_int_value);
      }
      else /* bit_size == 64 */
      {
        read_aligned_64(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &signed_long_long_value);
        return_value = LL2NUM(signed_long_long_value);
      }
    } else {
      string_length = ((bit_size - 1)/ 8) + 1;
      array_length = string_length + 4; /* Required number of bytes plus slack */
      unsigned_char_array = (unsigned char*) malloc(array_length);
      read_bitfield(lower_bound, upper_bound, bit_offset, bit_size, given_bit_offset, given_bit_size, param_endianness, buffer, (int)buffer_length, unsigned_char_array);

      num_words = ((string_length - 1) / 4) + 1;
      num_bytes = num_words * 4;
      num_bits = num_bytes * 8;
      shift_needed = num_bits - bit_size;
      shift_count = shift_needed / 8;
      shift_needed = shift_needed % 8;

      if (bit_size > 1) {
        for (index = 0; index < shift_count; index++) {
          signed_right_shift_byte_array(unsigned_char_array, num_bytes, 8);
        }

        if (shift_needed > 0) {
          signed_right_shift_byte_array(unsigned_char_array, num_bytes, shift_needed);
        }
      } else {
        for (index = 0; index < shift_count; index++) {
          unsigned_right_shift_byte_array(unsigned_char_array, num_bytes, 8);
        }

        if (shift_needed > 0) {
          unsigned_right_shift_byte_array(unsigned_char_array, num_bytes, shift_needed);
        }
      }

      if (HOST_ENDIANNESS == symbol_LITTLE_ENDIAN) {
        for (index = 0; index < num_bytes; index += 4) {
          reverse_bytes(&(unsigned_char_array[index]), 4);
        }
      }

      if (bit_size <= 31) {
        return_value = INT2FIX(*((int*) unsigned_char_array));
      } else if (bit_size == 32) {
        return_value = INT2NUM(*((int*) unsigned_char_array));
      } else {
        return_value = rb_int2big(*((int*) unsigned_char_array));
        temp_value = INT2FIX(32);
        for (index = 4; index < num_bytes; index += 4) {
          return_value = rb_big_lshift(return_value, temp_value);
          if (FIXNUM_P(return_value)) {
            signed_long_value = FIX2LONG(return_value);
            return_value = rb_int2big(signed_long_value);
          }
          return_value = rb_big_plus(return_value, rb_uint2big(*((unsigned int*) &(unsigned_char_array[index]))));
          if (FIXNUM_P(return_value)) {
            signed_long_value = FIX2LONG(return_value);
            return_value = rb_int2big(signed_long_value);
          }
        }
        return_value = rb_big_norm(return_value);
      }

      free(unsigned_char_array);
    }

  } else if (param_data_type == symbol_UINT) {

    /*###################################
       *# Handle :UINT data type
       *###################################*/

    if ((byte_aligned) && ((bit_size == 8) || (bit_size == 16) || (bit_size == 32) || (bit_size == 64))) {
      /*###########################################################
         *# Handle byte-aligned 8, 16, 32, and 64 bit :UINT
         *###########################################################*/

      if (bit_size == 8)
      {
        unsigned_char_value = buffer[lower_bound];
        return_value = INT2FIX(unsigned_char_value);
      }
      else if (bit_size == 16)
      {
        read_aligned_16(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &unsigned_short_value);
        return_value = INT2FIX(unsigned_short_value);
      }
      else if (bit_size == 32)
      {
        read_aligned_32(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &unsigned_int_value);
        return_value = UINT2NUM(unsigned_int_value);
      }
      else /* bit_size == 64 */
      {
        read_aligned_64(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &unsigned_long_long_value);
        return_value = ULL2NUM(unsigned_long_long_value);
      }
    } else {
      string_length = ((bit_size - 1)/ 8) + 1;
      array_length = string_length + 4; /* Required number of bytes plus slack */
      unsigned_char_array = (unsigned char*) malloc(array_length);
      read_bitfield(lower_bound, upper_bound, bit_offset, bit_size, given_bit_offset, given_bit_size, param_endianness, buffer, (int)buffer_length, unsigned_char_array);

      num_words = ((string_length - 1) / 4) + 1;
      num_bytes = num_words * 4;
      num_bits = num_bytes * 8;
      shift_needed = num_bits - bit_size;
      shift_count = shift_needed / 8;
      shift_needed = shift_needed % 8;

      for (index = 0; index < shift_count; index++) {
        unsigned_right_shift_byte_array(unsigned_char_array, num_bytes, 8);
      }

      if (shift_needed > 0) {
        unsigned_right_shift_byte_array(unsigned_char_array, num_bytes, shift_needed);
      }

      if (HOST_ENDIANNESS == symbol_LITTLE_ENDIAN) {
        for (index = 0; index < num_bytes; index += 4) {
          reverse_bytes(&(unsigned_char_array[index]), 4);
        }
      }

      if (bit_size <= 30) {
        return_value = INT2FIX(*((int*) unsigned_char_array));
      } else if (bit_size <= 32) {
        return_value = UINT2NUM(*((unsigned int*) unsigned_char_array));
      } else {
        return_value = rb_uint2big(*((unsigned int*) unsigned_char_array));
        temp_value = INT2FIX(32);
        for (index = 4; index < num_bytes; index += 4) {
          return_value = rb_big_lshift(return_value, temp_value);
          if (FIXNUM_P(return_value)) {
            signed_long_value = FIX2LONG(return_value);
            return_value = rb_int2big(signed_long_value);
          }
          return_value = rb_big_plus(return_value, rb_uint2big(*((unsigned int*) &(unsigned_char_array[index]))));
          if (FIXNUM_P(return_value)) {
            signed_long_value = FIX2LONG(return_value);
            return_value = rb_int2big(signed_long_value);
          }
        }
        return_value = rb_big_norm(return_value);
      }

      free(unsigned_char_array);
    }

  } else if (param_data_type == symbol_FLOAT) {

    /*##########################
       *# Handle :FLOAT data type
       *##########################*/

    if (byte_aligned) {
      switch (bit_size) {
        case 32:
          read_aligned_32(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &float_value);
          return_value = rb_float_new(float_value);
          break;

        case 64:
          read_aligned_64(lower_bound, upper_bound, param_endianness, buffer, (unsigned char*) &double_value);
          return_value = rb_float_new(double_value);
          break;

        default:
          rb_raise(rb_eArgError, "bit_size is %d but must be 32 or 64 for data_type %s", given_bit_size, RSTRING_PTR(rb_funcall(param_data_type, id_method_to_s, 0)));
          break;
      };
    } else {
      rb_raise(rb_eArgError, "bit_offset %d is not byte aligned for data_type %s", given_bit_offset, RSTRING_PTR(rb_funcall(param_data_type, id_method_to_s, 0)));
    }

  } else {

    /*############################
       *# Handle Unknown data types
       *############################*/

    rb_raise(rb_eArgError, "data_type %s is not recognized", RSTRING_PTR(rb_funcall(param_data_type, id_method_to_s, 0)));
  }

  return return_value;
}

/*
 * Returns the actual length as an integer.
 *
 *   get_int_length(self) #=> 324
 */
static int get_int_length(VALUE self)
{
  VALUE buffer = rb_ivar_get(self, id_ivar_buffer);
  if (RTEST(buffer)) {
    return (int)RSTRING_LEN(buffer);
  } else {
    return 0;
  }
}

/*
 * Returns the actual structure length.
 *
 *   structure.length #=> 324
 */
static VALUE structure_length(VALUE self) {
  return INT2FIX(get_int_length(self));
}

static VALUE read_item_internal(VALUE self, VALUE item, VALUE buffer) {
  VALUE bit_offset = Qnil;
  VALUE bit_size = Qnil;
  VALUE data_type = Qnil;
  VALUE array_size = Qnil;
  VALUE endianness = Qnil;

  data_type = rb_ivar_get(item, id_ivar_data_type);
  if (data_type == symbol_DERIVED) {
    return Qnil;
  }

  if (RTEST(buffer)) {
    bit_offset = rb_ivar_get(item, id_ivar_bit_offset);
    bit_size = rb_ivar_get(item, id_ivar_bit_size);
    array_size = rb_ivar_get(item, id_ivar_array_size);
    endianness = rb_ivar_get(item, id_ivar_endianness);
    if (RTEST(array_size)) {
      return rb_funcall(cBinaryAccessor, id_method_read_array, 6, bit_offset, bit_size, data_type, array_size, buffer, endianness);
    } else {
      return binary_accessor_read(cBinaryAccessor, bit_offset, bit_size, data_type, buffer, endianness);
    }
  } else {
    rb_raise(rb_eRuntimeError, "No buffer given to read_item");
  }
}

/*
 * Read an item in the structure
 *
 * @param item [StructureItem] Instance of StructureItem or one of its subclasses
 * @param value_type [Symbol] Not used. Subclasses should overload this
 *   parameter to check whether to perform conversions on the item.
 * @param buffer [String] The binary buffer to read the item from
 * @return Value based on the item definition. This could be a string, integer,
 *   float, or array of values.
 */
static VALUE read_item(int argc, VALUE* argv, VALUE self)
{
  VALUE item = Qnil;
  VALUE buffer = Qnil;

  switch (argc)
  {
    case 1:
    case 2:
      item = argv[0];
      buffer = rb_ivar_get(self, id_ivar_buffer);
      break;
    case 3:
      item = argv[0];
      buffer = argv[2];
      break;
    default:
      /* Invalid number of arguments given */
      rb_raise(rb_eArgError, "wrong number of arguments (%d for 1..3)", argc);
      break;
  };

  return read_item_internal(self, item, buffer);
}

/*
 * Comparison Operator based on bit_offset. This means that StructureItems
 * with different names or bit sizes are equal if they have the same bit
 * offset.
 */
static VALUE structure_item_spaceship(VALUE self, VALUE other_item) {
  int bit_offset = FIX2INT(rb_ivar_get(self, id_ivar_bit_offset));
  int other_bit_offset = FIX2INT(rb_ivar_get(other_item, id_ivar_bit_offset));
  int bit_size = 0;
  int other_bit_size = 0;

  /* Handle same bit offset case */
  if ((bit_offset == 0) && (other_bit_offset == 0)) {
    /* Both bit_offsets are 0 so sort by bit_size
      * This allows derived items with bit_size of 0 to be listed first
      * Compare based on bit size */
    bit_size = FIX2INT(rb_ivar_get(self, id_ivar_bit_size));
    other_bit_size = FIX2INT(rb_ivar_get(other_item, id_ivar_bit_size));
    if (bit_size == other_bit_size) {
      return INT2FIX(0);
    } if (bit_size < other_bit_size) {
      return INT2FIX(-1);
    } else {
      return INT2FIX(1);
    }
  }

  /* Handle different bit offsets */
  if (((bit_offset >= 0) && (other_bit_offset >= 0)) || ((bit_offset < 0) && (other_bit_offset < 0))) {
    /* Both Have Same Sign */
    if (bit_offset == other_bit_offset) {
      return INT2FIX(0);
    } else if (bit_offset < other_bit_offset) {
      return INT2FIX(-1);
    } else {
      return INT2FIX(1);
    }
  } else {
    /* Different Signs */
    if (bit_offset == other_bit_offset) {
      return INT2FIX(0);
    } else if (bit_offset < other_bit_offset) {
      return INT2FIX(1);
    } else {
      return INT2FIX(-1);
    }
  }
}

/* Structure constructor
 *
 * @param default_endianness [Symbol] Must be one of
 *   {BinaryAccessor::ENDIANNESS}. By default it uses
 *   BinaryAccessor::HOST_ENDIANNESS to determine the endianness of the host platform.
 * @param buffer [String] Buffer used to store the structure
 * @param item_class [Class] Class used to instantiate new structure items.
 *   Must be StructureItem or one of its subclasses.
 */
static VALUE structure_initialize(int argc, VALUE* argv, VALUE self) {
  VALUE default_endianness = Qnil;
  VALUE buffer = Qnil;
  VALUE item_class = Qnil;

  switch (argc)
  {
    case 0:
      default_endianness = HOST_ENDIANNESS;
      buffer = rb_str_new2("");
      item_class = cStructureItem;
      break;
    case 1:
      default_endianness = argv[0];
      buffer = rb_str_new2("");
      item_class = cStructureItem;
      break;
    case 2:
      default_endianness = argv[0];
      buffer = argv[1];
      item_class = cStructureItem;
      break;
    case 3:
      default_endianness = argv[0];
      buffer = argv[1];
      item_class = argv[2];
      break;
    default:
      /* Invalid number of arguments given */
      rb_raise(rb_eArgError, "wrong number of arguments (%d for 0..3)", argc);
      break;
  };

  if ((default_endianness == symbol_BIG_ENDIAN) || (default_endianness == symbol_LITTLE_ENDIAN)) {
    rb_ivar_set(self, id_ivar_default_endianness, default_endianness);
    if (RTEST(buffer)) {
      Check_Type(buffer, T_STRING);
      rb_funcall(buffer, id_method_force_encoding, 1, ASCII_8BIT_STRING);
      rb_ivar_set(self, id_ivar_buffer, buffer);
    } else {
      rb_ivar_set(self, id_ivar_buffer, Qnil);
    }
    rb_ivar_set(self, id_ivar_item_class, item_class);
    rb_ivar_set(self, id_ivar_items, rb_hash_new());
    rb_ivar_set(self, id_ivar_sorted_items, rb_ary_new());
    rb_ivar_set(self, id_ivar_defined_length, INT2FIX(0));
    rb_ivar_set(self, id_ivar_defined_length_bits, INT2FIX(0));
    rb_ivar_set(self, id_ivar_pos_bit_size, INT2FIX(0));
    rb_ivar_set(self, id_ivar_neg_bit_size, INT2FIX(0));
    rb_ivar_set(self, id_ivar_fixed_size, Qtrue);
    rb_ivar_set(self, id_ivar_short_buffer_allowed, Qfalse);
    rb_ivar_set(self, id_ivar_mutex, Qnil);
  } else {
    rb_raise(rb_eArgError, "Unrecognized endianness: %s - Must be :BIG_ENDIAN or :LITTLE_ENDIAN", RSTRING_PTR(rb_funcall(default_endianness, id_method_to_s, 0)));
  }

  return self;
}

/*
 * Resize the buffer at least the defined length of the structure
 */
static VALUE resize_buffer(VALUE self)
{
  VALUE buffer = rb_ivar_get(self, id_ivar_buffer);
  if (RTEST(buffer)) {
    VALUE value_defined_length = rb_ivar_get(self, id_ivar_defined_length);
    long defined_length = FIX2INT(value_defined_length);
    long current_length = RSTRING_LEN(buffer);

    /* Extend data size */
    if (current_length < defined_length)
    {
      rb_str_concat(buffer, rb_str_times(ZERO_STRING, INT2FIX(defined_length - current_length)));
    }
  }

  return self;
}

/*
 * Initialize all Packet methods
 */
void Init_structure (void)
{
  int zero = 0;

  id_method_to_s = rb_intern("to_s");
  id_method_raise_buffer_error = rb_intern("raise_buffer_error");
  id_method_read_array = rb_intern("read_array");
  id_method_force_encoding = rb_intern("force_encoding");
  id_method_freeze = rb_intern("freeze");

  ASCII_8BIT_STRING = rb_str_new2("ASCII-8BIT");
  rb_funcall(ASCII_8BIT_STRING, id_method_freeze, 0);

  ZERO_STRING = rb_str_new((char*) &zero, 1);
  rb_funcall(ZERO_STRING, id_method_freeze, 0);
  id_const_ZERO_STRING = rb_intern("ZERO_STRING");

  id_ivar_buffer = rb_intern("@buffer");
  id_ivar_bit_offset = rb_intern("@bit_offset");
  id_ivar_bit_size = rb_intern("@bit_size");
  id_ivar_array_size = rb_intern("@array_size");
  id_ivar_endianness = rb_intern("@endianness");
  id_ivar_data_type = rb_intern("@data_type");
  id_ivar_default_endianness = rb_intern("@default_endianness");
  id_ivar_item_class = rb_intern("@item_class");
  id_ivar_items = rb_intern("@items");
  id_ivar_sorted_items = rb_intern("@sorted_items");
  id_ivar_defined_length = rb_intern("@defined_length");
  id_ivar_defined_length_bits = rb_intern("@defined_length_bits");
  id_ivar_pos_bit_size = rb_intern("@pos_bit_size");
  id_ivar_neg_bit_size = rb_intern("@neg_bit_size");
  id_ivar_fixed_size = rb_intern("@fixed_size");
  id_ivar_short_buffer_allowed = rb_intern("@short_buffer_allowed");
  id_ivar_mutex = rb_intern("@mutex");

  symbol_LITTLE_ENDIAN = ID2SYM(rb_intern("LITTLE_ENDIAN"));
  symbol_BIG_ENDIAN = ID2SYM(rb_intern("BIG_ENDIAN"));
  symbol_INT = ID2SYM(rb_intern("INT"));
  symbol_UINT = ID2SYM(rb_intern("UINT"));
  symbol_FLOAT = ID2SYM(rb_intern("FLOAT"));
  symbol_STRING = ID2SYM(rb_intern("STRING"));
  symbol_BLOCK = ID2SYM(rb_intern("BLOCK"));
  symbol_DERIVED = ID2SYM(rb_intern("DERIVED"));
  symbol_read = ID2SYM(rb_intern("read"));

  if ((*((char *) &endianness_check)) == 1) {
    HOST_ENDIANNESS = symbol_LITTLE_ENDIAN;
  } else {
    HOST_ENDIANNESS = symbol_BIG_ENDIAN;
  }

  mCosmos = rb_define_module("Cosmos");

  cBinaryAccessor = rb_define_class_under(mCosmos, "BinaryAccessor", rb_cObject);
  rb_define_singleton_method(cBinaryAccessor, "read", binary_accessor_read, 5);

  cStructure = rb_define_class_under(mCosmos, "Structure", rb_cObject);
  rb_const_set(cStructure, id_const_ZERO_STRING, ZERO_STRING);
  rb_define_method(cStructure, "initialize", structure_initialize, -1);
  rb_define_method(cStructure, "length", structure_length, 0);
  rb_define_method(cStructure, "read_item", read_item, -1);
  rb_define_method(cStructure, "resize_buffer", resize_buffer, 0);

  cStructureItem = rb_define_class_under(mCosmos, "StructureItem", rb_cObject);
  rb_define_method(cStructureItem, "<=>", structure_item_spaceship, 1);
}