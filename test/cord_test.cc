#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nihterm/cord.h>

TEST(CordTests, cord_new_destroy) {
  struct cord *cord = cord_new();
  EXPECT_NE(cord, nullptr);
  cord_destroy(cord);
}

TEST(CordTests, cord_new_str) {
  char buf[64];
  struct cord *cord = cord_new_str("hello, world");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 12);
  EXPECT_STREQ(buf, "hello, world");
  cord_destroy(cord);
}

TEST(CordTests, cord_insertion_start) {
  char buf[64];
  struct cord *cord = cord_new_str("world");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_insert(cord, 0, "hello, "), 7);
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 12);
  EXPECT_STREQ(buf, "hello, world");
  cord_destroy(cord);
}

TEST(CordTests, cord_insertion_end) {
  char buf[64];
  struct cord *cord = cord_new_str("hello,");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_insert(cord, -1, " world"), 6);
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 12);
  EXPECT_STREQ(buf, "hello, world");
  cord_destroy(cord);
}

TEST(CordTests, cord_insertion_mid) {
  char buf[64];
  struct cord *cord = cord_new_str("helloworld");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_insert(cord, 5, ", "), 2);
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 12);
  EXPECT_STREQ(buf, "hello, world");
  cord_destroy(cord);
}

TEST(CordTests, cord_deletion_start) {
  char buf[64];
  struct cord *cord = cord_new_str("hehello, world");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_delete(cord, 0, 2), 2);
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 12);
  EXPECT_STREQ(buf, "hello, world");
  cord_destroy(cord);
}

TEST(CordTests, cord_deletion_end) {
  char buf[64];
  struct cord *cord = cord_new_str("hello, worldld");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_delete(cord, -1, 2), 2);
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 12);
  EXPECT_STREQ(buf, "hello, world");
  cord_destroy(cord);
}

TEST(CordTests, cord_deletion_mid) {
  char buf[64];
  struct cord *cord = cord_new_str("hello, ,,world");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_delete(cord, 7, 2), 2);
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 12);
  EXPECT_STREQ(buf, "hello, world");
  cord_destroy(cord);
}

TEST(CordTests, cord_split) {
  char buf[64];
  struct cord *cord = cord_new_str("hello, world");
  EXPECT_NE(cord, nullptr);
  struct cord *right = cord_split(cord, 6);
  EXPECT_NE(right, nullptr);

  // left side
  EXPECT_EQ(cord_string(cord, buf, sizeof(buf)), 6);
  EXPECT_STREQ(buf, "hello,");

  // right side
  EXPECT_EQ(cord_string(right, buf, sizeof(buf)), 6);
  EXPECT_STREQ(buf, " world");

  cord_destroy(right);
  cord_destroy(cord);
}

TEST(CordTests, cord_substring) {
  char buf[64];
  struct cord *cord = cord_new_str("hello, editing world");
  EXPECT_NE(cord, nullptr);
  EXPECT_EQ(cord_substring(cord, 7, buf, 7), 7);
  EXPECT_STREQ(buf, "editing");

  EXPECT_EQ(cord_substring(cord, 0, buf, 5), 5);
  EXPECT_STREQ(buf, "hello");

  EXPECT_EQ(cord_substring(cord, 15, buf, 16), 5);
  EXPECT_STREQ(buf, "world");

  cord_destroy(cord);
}
