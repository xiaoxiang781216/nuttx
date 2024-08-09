# ##############################################################################
# drivers/cpufreq/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

if(CONFIG_CPUFREQ)
  set(SRCS qos.c cpufreq.c freq_table.c)

  if(CONFIG_CPUFREQ_DEFAULT_GOV_PERFORMANCE)
    list(APPEND SRCS cpufreq_performance.c)
  endif()

  if(CONFIG_CPUFREQ_DEFAULT_GOV_POWERSAVE)
    list(APPEND SRCS cpufreq_powersave.c)
  endif()

  if(CONFIG_CPUFREQ_DEFAULT_GOV_ONDEMAND)
    list(APPEND SRCS cpufreq_ondemand.c)
  endif()

  if(CONFIG_CPUFREQ_PROCFS)
    list(APPEND SRCS cpufreq_procfs.c)
  endif()

  target_sources(drivers PRIVATE ${SRCS})
endif()
