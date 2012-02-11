namespace std {

%naturalvar string;

class string;

// string
%typemap(jni) string "jstring"
%typemap(jtype) string "String"
%typemap(jstype) string "String"
%typemap(javadirectorin) string "$jniinput"
%typemap(javadirectorout) string "$javacall"

%typemap(in) string
%{if(!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null std::string");
    return $null;
  }
  const jchar *$1_pstr = jenv->GetStringChars($input, 0);
  if (!$1_pstr) return $null;
  jsize $1_len = jenv->GetStringLength($input);
  if ($1_len) {
    $1.reserve($1_len);
    for (jsize i = 0; i < $1_len; ++i) {
      $1.push_back((wchar_t)$1_pstr[i]);
    }
  }
  jenv->ReleaseStringChars($input, $1_pstr);
 %}

%typemap(directorout) string
%{if(!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null std::string");
    return $null;
  }
  const jchar *$1_pstr = jenv->GetStringChars($input, 0);
  if (!$1_pstr) return $null;
  jsize $1_len = jenv->GetStringLength($input);
  if ($1_len) {
    $result.reserve($1_len);
    for (jsize i = 0; i < $1_len; ++i) {
      $result.push_back((wchar_t)$1_pstr[i]);
    }
  }
  jenv->ReleaseStringChars($input, $1_pstr);
 %}

%typemap(directorin,descriptor="Ljava/lang/String;") string {
  jsize $1_len = $1.length();
  jchar *conv_buf = new jchar[$1_len];
  for (jsize i = 0; i < $1_len; ++i) {
    conv_buf[i] = (jchar)$1[i];
  }
  $input = jenv->NewString(conv_buf, $1_len);
  delete [] conv_buf;
}

%typemap(out) string
%{jsize $1_len = $1.length();
  jchar *conv_buf = new jchar[$1_len];
  for (jsize i = 0; i < $1_len; ++i) {
    conv_buf[i] = (jchar)$1[i];
  }
  $result = jenv->NewString(conv_buf, $1_len);
  delete [] conv_buf; %}

%typemap(javain) string "$javainput"

%typemap(javaout) string {
    return $jnicall;
  }

//%typemap(typecheck) string = wchar_t *;

%typemap(throws) string
%{ std::string message($1.begin(), $1.end());
   SWIG_JavaThrowException(jenv, SWIG_JavaRuntimeException, message.c_str());
   return $null; %}

// const string &
%typemap(jni) const string & "jstring"
%typemap(jtype) const string & "String"
%typemap(jstype) const string & "String"
%typemap(javadirectorin) const string & "$jniinput"
%typemap(javadirectorout) const string & "$javacall"

%typemap(in) const string &
%{if(!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null std::string");
    return $null;
  }
  const jchar *$1_pstr = jenv->GetStringChars($input, 0);
  if (!$1_pstr) return $null;
  jsize $1_len = jenv->GetStringLength($input);
  std::string $1_str;
  if ($1_len) {
    $1_str.reserve($1_len);
    for (jsize i = 0; i < $1_len; ++i) {
      $1_str.push_back((wchar_t)$1_pstr[i]);
    }
  }
  $1 = &$1_str;
  jenv->ReleaseStringChars($input, $1_pstr);
 %}

%typemap(directorout,warning=SWIGWARN_TYPEMAP_THREAD_UNSAFE_MSG) const string & 
%{if(!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null std::string");
    return $null;
  }
  const jchar *$1_pstr = jenv->GetStringChars($input, 0);
  if (!$1_pstr) return $null;
  jsize $1_len = jenv->GetStringLength($input);
  /* possible thread/reentrant code problem */
  static std::string $1_str;
  if ($1_len) {
    $1_str.reserve($1_len);
    for (jsize i = 0; i < $1_len; ++i) {
      $1_str.push_back((wchar_t)$1_pstr[i]);
    }
  }
  $result = &$1_str;
  jenv->ReleaseStringChars($input, $1_pstr); %}

%typemap(directorin,descriptor="Ljava/lang/String;") const string & {
  jsize $1_len = $1.length();
  jchar *conv_buf = new jchar[$1_len];
  for (jsize i = 0; i < $1_len; ++i) {
    conv_buf[i] = (jchar)($1)[i];
  }
  $input = jenv->NewString(conv_buf, $1_len);
  delete [] conv_buf;
}

%typemap(out) const string & 
%{jsize $1_len = $1->length();
  jchar *conv_buf = new jchar[$1_len];
  for (jsize i = 0; i < $1_len; ++i) {
    conv_buf[i] = (jchar)(*$1)[i];
  }
  $result = jenv->NewString(conv_buf, $1_len);
  delete [] conv_buf; %}

%typemap(javain) const string & "$javainput"

%typemap(javaout) const string & {
    return $jnicall;
  }

//%typemap(typecheck) const string & = wchar_t *;

%typemap(throws) const string &
%{ std::string message($1.begin(), $1.end());
   SWIG_JavaThrowException(jenv, SWIG_JavaRuntimeException, message.c_str());
   return $null; %}

}

