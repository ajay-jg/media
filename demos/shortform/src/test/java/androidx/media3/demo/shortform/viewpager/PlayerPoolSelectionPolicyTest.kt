/*
 * Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package androidx.media3.demo.shortform.viewpager

import org.junit.Assert.assertFalse
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PlayerPoolSelectionPolicyTest {

  @Test
  fun singlePlayerPool_onlySelectedPageCanAcquirePlayer() {
    val policy = PlayerPoolSelectionPolicy(poolSize = 1)

    assertTrue(policy.shouldAcquirePlayer(position = 3, selectedPosition = 3))
    assertFalse(policy.shouldAcquirePlayer(position = 2, selectedPosition = 3))
  }

  @Test
  fun multiPlayerPool_allAttachedPagesCanAcquirePlayer() {
    val policy = PlayerPoolSelectionPolicy(poolSize = 2)

    assertTrue(policy.shouldAcquirePlayer(position = 2, selectedPosition = 3))
  }

  @Test
  fun singlePlayerPool_releasesNonSelectedAttachedPages() {
    val policy = PlayerPoolSelectionPolicy(poolSize = 1)

    assertEquals(
      listOf(2, 4),
      policy.positionsToReleaseOnSelection(listOf(2, 3, 4), selectedPosition = 3),
    )
  }
}
