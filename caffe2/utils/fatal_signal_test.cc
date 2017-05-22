#if defined(__linux__)
#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>

#include <functional>
#include <iostream>

#include "caffe2/core/common.h"
#include "caffe2/utils/signal_handler.h"

namespace {
void* dummy_thread(void*) {
  while (1) {
  }
}

bool forkAndPipe(
    std::string& stderrBuffer,
    std::function<void(void)> callback) {
  std::array<int, 2> stderrPipe;
  if (pipe(stderrPipe.data()) != 0) {
    perror("STDERR pipe");
    return false;
  }
  pid_t child = fork();
  if (child == 0) {
    // Replace this process' stderr so we can read it.
    if (dup2(stderrPipe[1], STDERR_FILENO) < 0) {
      close(stderrPipe[0]);
      close(stderrPipe[1]);
      perror("dup2 STDERR");
      exit(1);
    }

    // This is for the parent to work with.
    close(stderrPipe[0]);
    close(stderrPipe[1]);

    // Install our handlers because gtest installs their own it seems.
    int argc = 0;
    char** argv = nullptr;
    if (!caffe2::internal::Caffe2InitFatalSignalHandler(&argc, &argv)) {
      write(STDERR_FILENO, "WAT\n", 4);
      exit(1);
    }
    callback();
    exit(1);
  } else if (child > 0) {
    const int bufferSize = 128;
    std::array<char, bufferSize> buffer;

    // We want to close the writing end of the pipe right away so our
    // read actually gets an EOF.
    close(stderrPipe[1]);

    // wait for child to finish crashing.
    int statloc;
    if (wait(&statloc) < 0) {
      close(stderrPipe[0]);
      perror("wait");
      return false;
    }

    // The child should have exited due to signal.
    if (!WIFSIGNALED(statloc)) {
      fprintf(stderr, "Child didn't exit because it received a signal\n");
      return false;
    }

    ssize_t bytesRead;
    while ((bytesRead = read(stderrPipe[0], buffer.data(), bufferSize)) > 0) {
      const std::string tmp(buffer.data(), bytesRead);
      std::cout << tmp;
      stderrBuffer += tmp;
    }
    if (bytesRead < 0) {
      perror("read");
      return false;
    }
    close(stderrPipe[0]);
    return true;
  } else {
    perror("fork");
    return false;
  }
}

#define TEST_FATAL_SIGNAL(signum, name, threadCount)                         \
  do {                                                                       \
    std::string stderrBuffer;                                                \
    ASSERT_TRUE(forkAndPipe(stderrBuffer, [=]() {                            \
      pthread_t pt;                                                          \
      for (int i = 0; i < threadCount; i++) {                                \
        if (pthread_create(&pt, nullptr, ::dummy_thread, nullptr)) {         \
          perror("pthread_create");                                          \
        }                                                                    \
      }                                                                      \
      raise(signum);                                                         \
    }));                                                                     \
    int keyPhraseCount = 0;                                                  \
    std::string keyPhrase =                                                  \
        std::string(name) + "(" + caffe2::to_string(signum) + "), Thread";   \
    size_t loc = 0;                                                          \
    while ((loc = stderrBuffer.find(keyPhrase, loc)) != std::string::npos) { \
      keyPhraseCount += 1;                                                   \
      loc += 1;                                                              \
    }                                                                        \
    EXPECT_EQ(keyPhraseCount, threadCount + 1);                              \
  } while (0)
}

TEST(fatalSignalTest, SIGABRT8) {
  TEST_FATAL_SIGNAL(SIGABRT, "SIGABRT", 8);
}

TEST(fatalSignalTest, SIGINT8) {
  TEST_FATAL_SIGNAL(SIGINT, "SIGINT", 8);
}

TEST(fatalSignalTest, SIGILL8) {
  TEST_FATAL_SIGNAL(SIGILL, "SIGILL", 8);
}

TEST(fatalSignalTest, SIGFPE8) {
  TEST_FATAL_SIGNAL(SIGFPE, "SIGFPE", 8);
}

TEST(fatalSignalTest, SIGBUS8) {
  TEST_FATAL_SIGNAL(SIGBUS, "SIGBUS", 8);
}

TEST(fatalSignalTest, SIGSEGV8) {
  TEST_FATAL_SIGNAL(SIGSEGV, "SIGSEGV", 8);
}
#endif // defined(__linux__)