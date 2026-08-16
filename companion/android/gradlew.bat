@ECHO OFF

SET APP_HOME=%~dp0
SET CLASSPATH=%APP_HOME%\gradle\wrapper\gradle-wrapper.jar

IF DEFINED JAVA_HOME (
    SET JAVACMD=%JAVA_HOME%\bin\java.exe
) ELSE (
    SET JAVACMD=java.exe
)

%JAVACMD% %GRADLE_OPTS% %JAVA_OPTS% -Dorg.gradle.appname=gradlew -classpath "%CLASSPATH%" org.gradle.wrapper.GradleWrapperMain %*
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
